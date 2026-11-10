#include "Network/IoContext.h"

#include "Network/NetworkError.h"
#include "Network/src/ReactorPoller.h"
#include "Thread/ThreadPool.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <exception>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#endif

namespace X_Y::detail
{
    namespace
    {
        [[noreturn]] void ThrowResolveError(int error, const char *detail)
        {
            throw NetworkError("getaddrinfo", error, detail);
        }

        std::vector<SocketAddr> ResolveTcpAddresses(
            const std::string &host,
            std::uint16_t port)
        {
#ifdef _WIN32
            WSADATA data{};
            const int startupError = WSAStartup(MAKEWORD(2, 2), &data);
            if (startupError != 0)
                ThrowResolveError(
                    startupError,
                    ("WSAStartup failed with error " + std::to_string(startupError)).c_str());
            struct WinsockCleanup
            {
                ~WinsockCleanup() { WSACleanup(); }
            } cleanup;
#endif

            const std::string service = std::to_string(port);
            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;

            addrinfo *rawAddresses = nullptr;
            const int error =
                getaddrinfo(host.c_str(), service.c_str(), &hints, &rawAddresses);
            if (error != 0)
            {
#ifdef _WIN32
                ThrowResolveError(
                    error,
                    ("Winsock getaddrinfo error " + std::to_string(error)).c_str());
#else
                ThrowResolveError(error, gai_strerror(error));
#endif
            }

            const std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addresses(
                rawAddresses,
                &freeaddrinfo);
            std::vector<SocketAddr> result;
            for (const addrinfo *entry = addresses.get();
                 entry != nullptr;
                 entry = entry->ai_next)
            {
                if (entry->ai_addr == nullptr)
                    continue;

                SocketAddr address{};
                if (entry->ai_addr->sa_family == AF_INET)
                {
                    const auto *ipv4 =
                        reinterpret_cast<const sockaddr_in *>(entry->ai_addr);
                    address.family = AddressFamily::IPv4;
                    address.port = ntohs(ipv4->sin_port);
                    std::memcpy(address.ipBytes, &ipv4->sin_addr, 4);
                }
                else if (entry->ai_addr->sa_family == AF_INET6)
                {
                    const auto *ipv6 =
                        reinterpret_cast<const sockaddr_in6 *>(entry->ai_addr);
                    address.family = AddressFamily::IPv6;
                    address.port = ntohs(ipv6->sin6_port);
                    address.scopeId = ipv6->sin6_scope_id;
                    std::memcpy(address.ipBytes, &ipv6->sin6_addr, 16);
                }
                else
                {
                    continue;
                }
                result.push_back(address);
            }

            if (result.empty())
                throw NetworkError("getaddrinfo", 0, "no usable address was returned");
            return result;
        }

        ThreadPool &GetResolverPool()
        {
            static ThreadPool pool(2);
            return pool;
        }
    }

    class ResolveRequestState
    {
    public:
        enum class Status : std::uint8_t
        {
            Pending,
            Cancelled,
            Completed
        };

        std::atomic<Status> status{Status::Pending};
    };

    class IoContextState
    {
    public:
        using Clock = std::chrono::steady_clock;
        using TimerDeadlines = std::multimap<Clock::time_point, std::uint64_t>;

        struct WatchEntry
        {
            std::uintptr_t nativeSocket = InvalidNativeSocketHandle;
            IoInterest interest = IoInterest::Read;
            IoContext::Handler handler;
        };

        struct TimerEntry
        {
            TimerDeadlines::iterator deadline;
            std::function<void()> handler;
        };

        IoContextState()
            : m_Poller(CreateReactorPoller())
        {
            if (!m_Poller)
                throw std::runtime_error("Reactor poller factory returned no implementation");
        }

        std::uint64_t AddWatch(
            SocketHandle &socket,
            IoInterest interest,
            IoContext::Handler handler)
        {
            if (!IsValidInterest(interest))
                throw std::invalid_argument("Unsupported socket readiness interest");
            if (!handler)
                throw std::invalid_argument("Socket readiness handler must not be empty");

            std::lock_guard lock(m_Mutex);
            if (m_Stopped.load(std::memory_order_acquire))
                throw std::logic_error("Cannot watch sockets on a stopped IoContext");
            if (!socket.IsValid())
                throw std::invalid_argument("Cannot watch an invalid socket");
            if (!socket.SetNonBlocking(true))
                throw NetworkError(
                    "set non-blocking",
                    0,
                    "failed to enable non-blocking mode for Reactor socket");
            const std::uintptr_t nativeSocket = socket.GetNativeHandle();
            if (nativeSocket == InvalidNativeSocketHandle)
                throw std::invalid_argument("Cannot watch an invalid socket");

            if (m_NextId == 0 || m_NextId == std::numeric_limits<std::uint64_t>::max())
                throw std::overflow_error("IoContext registration id space is exhausted");

            const std::uint64_t id = m_NextId++;
            m_Poller->Add(id, nativeSocket, interest);
            try
            {
                m_Watches.emplace(id, WatchEntry{nativeSocket, interest, std::move(handler)});
            }
            catch (...)
            {
                m_Poller->Remove(id);
                throw;
            }
            return id;
        }

        std::uint64_t AddTimer(
            std::chrono::milliseconds delay,
            std::function<void()> handler)
        {
            if (delay < std::chrono::milliseconds::zero())
                throw std::invalid_argument("Timer delay must not be negative");
            if (!handler)
                throw std::invalid_argument("Timer handler must not be empty");

            const auto now = Clock::now();
            const auto maxDelay =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    Clock::time_point::max() - now);
            if (delay > maxDelay)
                throw std::out_of_range("Timer delay exceeds the supported range");
            const auto deadline = now + delay;

            std::lock_guard lock(m_Mutex);
            if (m_Stopped.load(std::memory_order_acquire))
                throw std::logic_error("Cannot schedule timers on a stopped IoContext");
            if (m_NextTimerId == 0 ||
                m_NextTimerId == std::numeric_limits<std::uint64_t>::max())
                throw std::overflow_error("IoContext timer id space is exhausted");

            const std::uint64_t id = m_NextTimerId++;
            const auto deadlineEntry = m_TimerDeadlines.emplace(deadline, id);
            try
            {
                m_Timers.emplace(
                    id,
                    TimerEntry{deadlineEntry, std::move(handler)});
            }
            catch (...)
            {
                m_TimerDeadlines.erase(deadlineEntry);
                throw;
            }

            try
            {
                m_Poller->Wake();
            }
            catch (...)
            {
                m_Timers.erase(id);
                m_TimerDeadlines.erase(deadlineEntry);
                throw;
            }
            return id;
        }

        bool Post(std::function<void()> task)
        {
            if (!task)
                throw std::invalid_argument("Posted task must not be empty");

            std::lock_guard lock(m_Mutex);
            if (m_Stopped.load(std::memory_order_acquire))
                return false;

            m_PostedTasks.push_back(std::move(task));
            try
            {
                m_Poller->Wake();
            }
            catch (...)
            {
                m_PostedTasks.pop_back();
                throw;
            }
            return true;
        }

        bool IsStopped() const noexcept
        {
            return m_Stopped.load(std::memory_order_acquire);
        }

        bool CancelTimer(std::uint64_t id) noexcept
        {
            {
                std::lock_guard lock(m_Mutex);
                const auto found = m_Timers.find(id);
                if (found == m_Timers.end())
                    return false;
                m_TimerDeadlines.erase(found->second.deadline);
                m_Timers.erase(found);
            }

            try
            {
                m_Poller->Wake();
            }
            catch (...)
            {
            }
            return true;
        }

        bool UpdateWatch(std::uint64_t id, IoInterest interest)
        {
            if (!IsValidInterest(interest))
                throw std::invalid_argument("Unsupported socket readiness interest");

            std::lock_guard lock(m_Mutex);
            if (m_Stopped.load(std::memory_order_acquire))
                return false;
            const auto found = m_Watches.find(id);
            if (found == m_Watches.end())
                return false;
            if (!m_Poller->Update(id, interest))
                return false;
            found->second.interest = interest;
            return true;
        }

        void RemoveWatch(std::uint64_t id) noexcept
        {
            std::lock_guard lock(m_Mutex);
            if (m_Watches.erase(id) != 0)
                m_Poller->Remove(id);
        }

        void Run()
        {
            bool expected = false;
            if (!m_Running.compare_exchange_strong(expected, true))
                throw std::logic_error("IoContext::Run is already active");

            struct RunningReset
            {
                std::atomic<bool> &running;
                ~RunningReset() { running.store(false, std::memory_order_release); }
            } reset{m_Running};

            while (!m_Stopped.load(std::memory_order_acquire))
            {
                auto task = TakePostedTask();
                if (task && !m_Stopped.load(std::memory_order_acquire))
                    (*task)();

                auto timerHandler = TakeExpiredTimer();
                if (timerHandler && !m_Stopped.load(std::memory_order_acquire))
                {
                    timerHandler();
                }

                if (m_Stopped.load(std::memory_order_acquire))
                    break;

                const auto events = m_Poller->Wait(GetNextWaitDuration());
                for (const auto &event : events)
                {
                    if (m_Stopped.load(std::memory_order_acquire))
                        break;

                    IoContext::Handler handler;
                    {
                        std::lock_guard lock(m_Mutex);
                        const auto found = m_Watches.find(event.id);
                        if (found == m_Watches.end())
                            continue;
                        handler = found->second.handler;
                    }

                    handler(event.ready);
                }
            }
        }

        std::function<void()> TakeExpiredTimer()
        {
            std::lock_guard lock(m_Mutex);
            while (!m_TimerDeadlines.empty() &&
                   m_TimerDeadlines.begin()->first <= Clock::now())
            {
                const auto deadline = m_TimerDeadlines.begin();
                const auto found = m_Timers.find(deadline->second);
                if (found == m_Timers.end())
                {
                    m_TimerDeadlines.erase(deadline);
                    continue;
                }

                auto handler = std::move(found->second.handler);
                m_Timers.erase(found);
                m_TimerDeadlines.erase(deadline);
                return handler;
            }
            return {};
        }

        std::optional<std::function<void()>> TakePostedTask()
        {
            std::lock_guard lock(m_Mutex);
            if (m_PostedTasks.empty())
                return std::nullopt;
            auto task = std::move(m_PostedTasks.front());
            m_PostedTasks.pop_front();
            return task;
        }

        std::chrono::milliseconds GetNextWaitDuration()
        {
            constexpr auto maxWait = std::chrono::milliseconds(100);
            std::lock_guard lock(m_Mutex);
            if (!m_PostedTasks.empty())
                return std::chrono::milliseconds::zero();
            if (m_TimerDeadlines.empty())
                return maxWait;

            const auto remaining = m_TimerDeadlines.begin()->first - Clock::now();
            if (remaining <= Clock::duration::zero())
                return std::chrono::milliseconds::zero();

            auto timeout =
                std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
            if (timeout < remaining)
                ++timeout;
            return timeout < maxWait ? timeout : maxWait;
        }

        void Stop() noexcept
        {
            bool shouldWake = false;
            {
                std::lock_guard lock(m_Mutex);
                shouldWake = !m_Stopped.exchange(true, std::memory_order_acq_rel);
            }
            if (shouldWake)
            {
                try
                {
                    m_Poller->Wake();
                }
                catch (...)
                {
                }
            }
        }

    private:
        static bool IsValidInterest(IoInterest interest) noexcept
        {
            return interest == IoInterest::Read ||
                   interest == IoInterest::Write ||
                   interest == IoInterest::ReadWrite;
        }

        std::unique_ptr<ReactorPoller> m_Poller;
        std::mutex m_Mutex;
        std::unordered_map<std::uint64_t, WatchEntry> m_Watches;
        std::deque<std::function<void()>> m_PostedTasks;
        TimerDeadlines m_TimerDeadlines;
        std::unordered_map<std::uint64_t, TimerEntry> m_Timers;
        std::uint64_t m_NextId = 1;
        std::uint64_t m_NextTimerId = 1;
        std::atomic<bool> m_Running{false};
        std::atomic<bool> m_Stopped{false};
    };
}

namespace X_Y
{
    IoContext::Registration::Registration(
        std::weak_ptr<detail::IoContextState> state,
        std::uint64_t id) noexcept
        : m_State(std::move(state)), m_Id(id)
    {
    }

    IoContext::Registration::~Registration()
    {
        Reset();
    }

    IoContext::Registration::Registration(Registration &&other) noexcept
        : m_State(std::move(other.m_State)), m_Id(std::exchange(other.m_Id, 0))
    {
    }

    IoContext::Registration &IoContext::Registration::operator=(Registration &&other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_State = std::move(other.m_State);
            m_Id = std::exchange(other.m_Id, 0);
        }
        return *this;
    }

    bool IoContext::Registration::Update(IoInterest interest)
    {
        if (m_Id == 0)
            return false;
        const auto state = m_State.lock();
        return state && state->UpdateWatch(m_Id, interest);
    }

    void IoContext::Registration::Reset() noexcept
    {
        if (m_Id == 0)
            return;
        if (const auto state = m_State.lock())
            state->RemoveWatch(m_Id);
        m_Id = 0;
        m_State.reset();
    }

    IoContext::Timer::Timer(
        std::weak_ptr<detail::IoContextState> state,
        std::uint64_t id) noexcept
        : m_State(std::move(state)), m_Id(id)
    {
    }

    IoContext::Timer::~Timer()
    {
        Reset();
    }

    IoContext::Timer::Timer(Timer &&other) noexcept
        : m_State(std::move(other.m_State)), m_Id(std::exchange(other.m_Id, 0))
    {
    }

    IoContext::Timer &IoContext::Timer::operator=(Timer &&other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_State = std::move(other.m_State);
            m_Id = std::exchange(other.m_Id, 0);
        }
        return *this;
    }

    bool IoContext::Timer::Cancel() noexcept
    {
        if (m_Id == 0)
            return false;
        const auto state = m_State.lock();
        const bool cancelled = state && state->CancelTimer(m_Id);
        m_Id = 0;
        m_State.reset();
        return cancelled;
    }

    void IoContext::Timer::Reset() noexcept
    {
        (void)Cancel();
    }

    IoContext::ResolveRequest::ResolveRequest(
        std::shared_ptr<detail::ResolveRequestState> state) noexcept
        : m_State(std::move(state))
    {
    }

    IoContext::ResolveRequest::~ResolveRequest()
    {
        (void)Cancel();
    }

    IoContext::ResolveRequest::ResolveRequest(ResolveRequest &&other) noexcept
        : m_State(std::move(other.m_State))
    {
    }

    IoContext::ResolveRequest &IoContext::ResolveRequest::operator=(
        ResolveRequest &&other) noexcept
    {
        if (this != &other)
        {
            (void)Cancel();
            m_State = std::move(other.m_State);
        }
        return *this;
    }

    bool IoContext::ResolveRequest::Cancel() noexcept
    {
        if (!m_State)
            return false;
        auto expected = detail::ResolveRequestState::Status::Pending;
        const bool cancelled = m_State->status.compare_exchange_strong(
            expected,
            detail::ResolveRequestState::Status::Cancelled,
            std::memory_order_acq_rel);
        m_State.reset();
        return cancelled;
    }

    bool IoContext::ResolveRequest::IsPending() const noexcept
    {
        return m_State &&
               m_State->status.load(std::memory_order_acquire) ==
                   detail::ResolveRequestState::Status::Pending;
    }

    IoContext::IoContext()
        : m_State(std::make_shared<detail::IoContextState>())
    {
    }

    IoContext::~IoContext()
    {
        Stop();
    }

    IoContext::Registration IoContext::Watch(
        SocketHandle &socket,
        IoInterest interest,
        Handler handler)
    {
        if (!m_State)
            throw std::logic_error("IoContext has been moved from");
        const auto id = m_State->AddWatch(socket, interest, std::move(handler));
        return Registration(m_State, id);
    }

    IoContext::Timer IoContext::ScheduleAfter(
        std::chrono::milliseconds delay,
        std::function<void()> handler)
    {
        if (!m_State)
            throw std::logic_error("IoContext has been moved from");
        const auto id = m_State->AddTimer(delay, std::move(handler));
        return Timer(m_State, id);
    }

    bool IoContext::Post(std::function<void()> task)
    {
        if (!m_State)
            throw std::logic_error("IoContext has been moved from");
        return m_State->Post(std::move(task));
    }

    IoContext::ResolveRequest IoContext::ResolveAsync(
        std::string host,
        std::uint16_t port,
        ResolveHandler handler)
    {
        if (!m_State)
            throw std::logic_error("IoContext has been moved from");
        if (host.empty())
            throw std::invalid_argument("Resolve host must not be empty");
        if (port == 0)
            throw std::invalid_argument("Resolve port must not be zero");
        if (!handler)
            throw std::invalid_argument("Resolve handler must not be empty");
        if (m_State->IsStopped())
            throw std::logic_error("Cannot resolve names on a stopped IoContext");

        auto requestState = std::make_shared<detail::ResolveRequestState>();
        const std::weak_ptr<detail::IoContextState> weakIoState = m_State;
        detail::GetResolverPool().Submit(
            [weakIoState,
             requestState,
             host = std::move(host),
             port,
             handler = std::move(handler)]() mutable
            {
                std::vector<SocketAddr> addresses;
                std::exception_ptr error;
                try
                {
                    addresses = detail::ResolveTcpAddresses(host, port);
                }
                catch (...)
                {
                    error = std::current_exception();
                }

                const auto ioState = weakIoState.lock();
                if (!ioState)
                {
                    requestState->status.store(
                        detail::ResolveRequestState::Status::Cancelled,
                        std::memory_order_release);
                    return;
                }

                const bool posted = ioState->Post(
                    [requestState,
                     addresses = std::move(addresses),
                     error,
                     handler = std::move(handler)]() mutable
                    {
                        auto expected =
                            detail::ResolveRequestState::Status::Pending;
                        if (!requestState->status.compare_exchange_strong(
                                expected,
                                detail::ResolveRequestState::Status::Completed,
                                std::memory_order_acq_rel))
                            return;
                        handler(std::move(addresses), error);
                    });
                if (!posted)
                {
                    requestState->status.store(
                        detail::ResolveRequestState::Status::Cancelled,
                        std::memory_order_release);
                }
            });
        return ResolveRequest(std::move(requestState));
    }

    void IoContext::Run()
    {
        if (!m_State)
            throw std::logic_error("IoContext has been moved from");
        m_State->Run();
    }

    void IoContext::Stop() noexcept
    {
        if (m_State)
            m_State->Stop();
    }

    bool IoContext::IsStopped() const noexcept
    {
        return !m_State || m_State->IsStopped();
    }
}
