#include "Network/IoContext.h"

#include "Network/NetworkError.h"
#include "Network/src/ReactorPoller.h"

#include <atomic>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace X_Y::detail
{
    class IoContextState
    {
    public:
        struct WatchEntry
        {
            std::uintptr_t nativeSocket = InvalidNativeSocketHandle;
            IoInterest interest = IoInterest::Read;
            IoContext::Handler handler;
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
                const auto events = m_Poller->Wait();
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

        bool IsStopped() const noexcept
        {
            return m_Stopped.load(std::memory_order_acquire);
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
        std::uint64_t m_NextId = 1;
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
