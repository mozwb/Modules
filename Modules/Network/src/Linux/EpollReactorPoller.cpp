#include "Network/src/ReactorPoller.h"
#include "Network/NetworkError.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace X_Y::detail
{
    namespace
    {
        [[noreturn]] void ThrowSystemError(const char *operation, int error)
        {
            throw NetworkError(
                operation,
                error,
                std::error_code(error, std::system_category()).message());
        }

        std::uint32_t ToEpollEvents(IoInterest interest)
        {
            std::uint32_t events = EPOLLRDHUP;
            if (interest == IoInterest::Read || interest == IoInterest::ReadWrite)
                events |= EPOLLIN;
            if (interest == IoInterest::Write || interest == IoInterest::ReadWrite)
                events |= EPOLLOUT;
            if (interest != IoInterest::Read &&
                interest != IoInterest::Write &&
                interest != IoInterest::ReadWrite)
                throw std::invalid_argument("Unsupported socket readiness interest");
            return events;
        }

        class EpollReactorPoller final : public ReactorPoller
        {
        public:
            EpollReactorPoller()
            {
                m_Epoll = ::epoll_create1(EPOLL_CLOEXEC);
                if (m_Epoll < 0)
                    ThrowSystemError("epoll_create1", errno);

                m_Wakeup = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
                if (m_Wakeup < 0)
                {
                    const int error = errno;
                    ::close(m_Epoll);
                    m_Epoll = -1;
                    ThrowSystemError("eventfd", error);
                }

                epoll_event event{};
                event.events = EPOLLIN;
                event.data.u64 = 0;
                if (::epoll_ctl(m_Epoll, EPOLL_CTL_ADD, m_Wakeup, &event) != 0)
                {
                    const int error = errno;
                    ::close(m_Wakeup);
                    ::close(m_Epoll);
                    m_Wakeup = -1;
                    m_Epoll = -1;
                    ThrowSystemError("epoll_ctl", error);
                }
            }

            ~EpollReactorPoller() override
            {
                if (m_Wakeup >= 0)
                    ::close(m_Wakeup);
                if (m_Epoll >= 0)
                    ::close(m_Epoll);
            }

            void Add(
                std::uint64_t id,
                std::uintptr_t nativeSocket,
                IoInterest interest) override
            {
                if (id == 0)
                    throw std::invalid_argument("Reactor registration id must not be zero");
                if (nativeSocket == InvalidNativeSocketHandle)
                    throw std::invalid_argument("Cannot poll an invalid socket");
                if (nativeSocket > static_cast<std::uintptr_t>(std::numeric_limits<int>::max()))
                    throw std::out_of_range("Native socket handle exceeds Linux descriptor range");

                const int socket = static_cast<int>(nativeSocket);
                epoll_event event{};
                event.events = ToEpollEvents(interest);
                event.data.u64 = id;

                std::lock_guard lock(m_Mutex);
                if (!m_Watches.emplace(id, Watch{socket, interest}).second)
                    throw std::logic_error("Duplicate Reactor registration id");
                if (::epoll_ctl(m_Epoll, EPOLL_CTL_ADD, socket, &event) != 0)
                {
                    const int error = errno;
                    m_Watches.erase(id);
                    ThrowSystemError("epoll_ctl add", error);
                }
                try
                {
                    Wake();
                }
                catch (...)
                {
                    ::epoll_ctl(m_Epoll, EPOLL_CTL_DEL, socket, nullptr);
                    m_Watches.erase(id);
                    throw;
                }
            }

            bool Update(std::uint64_t id, IoInterest interest) override
            {
                epoll_event event{};
                event.events = ToEpollEvents(interest);
                event.data.u64 = id;

                std::lock_guard lock(m_Mutex);
                const auto found = m_Watches.find(id);
                if (found == m_Watches.end())
                    return false;

                const IoInterest previous = found->second.interest;
                if (::epoll_ctl(
                        m_Epoll,
                        EPOLL_CTL_MOD,
                        found->second.socket,
                        &event) != 0)
                    ThrowSystemError("epoll_ctl modify", errno);
                found->second.interest = interest;
                try
                {
                    Wake();
                }
                catch (...)
                {
                    event.events = ToEpollEvents(previous);
                    ::epoll_ctl(
                        m_Epoll,
                        EPOLL_CTL_MOD,
                        found->second.socket,
                        &event);
                    found->second.interest = previous;
                    throw;
                }
                return true;
            }

            void Remove(std::uint64_t id) noexcept override
            {
                {
                    std::lock_guard lock(m_Mutex);
                    const auto found = m_Watches.find(id);
                    if (found == m_Watches.end())
                        return;
                    ::epoll_ctl(
                        m_Epoll,
                        EPOLL_CTL_DEL,
                        found->second.socket,
                        nullptr);
                    m_Watches.erase(found);
                }
                try
                {
                    Wake();
                }
                catch (...)
                {
                }
            }

            std::vector<PollerEvent> Wait(std::chrono::milliseconds timeout) override
            {
                std::array<epoll_event, 64> readyEvents{};
                const int count = ::epoll_wait(
                    m_Epoll,
                    readyEvents.data(),
                    static_cast<int>(readyEvents.size()),
                    static_cast<int>(
                        std::min(timeout, std::chrono::milliseconds(100)).count()));
                if (count < 0)
                {
                    const int error = errno;
                    if (error == EINTR)
                        return {};
                    ThrowSystemError("epoll_wait", error);
                }

                std::vector<PollerEvent> events;
                events.reserve(static_cast<std::size_t>(count));
                for (int index = 0; index < count; ++index)
                {
                    const auto &event = readyEvents[static_cast<std::size_t>(index)];
                    if (event.data.u64 == 0)
                    {
                        DrainWakeup();
                        continue;
                    }

                    std::uint8_t ready = 0;
                    if ((event.events & (EPOLLIN | EPOLLPRI)) != 0)
                        ready |= static_cast<std::uint8_t>(IoReady::Read);
                    if ((event.events & EPOLLOUT) != 0)
                        ready |= static_cast<std::uint8_t>(IoReady::Write);
                    if ((event.events & EPOLLERR) != 0)
                        ready |= static_cast<std::uint8_t>(IoReady::Error);
                    if ((event.events & (EPOLLHUP | EPOLLRDHUP)) != 0)
                        ready |= static_cast<std::uint8_t>(IoReady::Closed);
                    events.push_back({event.data.u64, static_cast<IoReady>(ready)});
                }
                return events;
            }

            void Wake() override
            {
                const std::uint64_t signal = 1;
                for (;;)
                {
                    const ssize_t result = ::write(m_Wakeup, &signal, sizeof(signal));
                    if (result == static_cast<ssize_t>(sizeof(signal)))
                        return;
                    if (result < 0 && errno == EINTR)
                        continue;
                    if (result < 0 && errno == EAGAIN)
                        return;
                    ThrowSystemError("eventfd write", result < 0 ? errno : EIO);
                }
            }

        private:
            void DrainWakeup()
            {
                std::uint64_t signal = 0;
                for (;;)
                {
                    const ssize_t result = ::read(m_Wakeup, &signal, sizeof(signal));
                    if (result == static_cast<ssize_t>(sizeof(signal)))
                        continue;
                    if (result < 0 && errno == EINTR)
                        continue;
                    if (result < 0 && errno == EAGAIN)
                        return;
                    ThrowSystemError("eventfd read", result < 0 ? errno : EIO);
                }
            }

            struct Watch
            {
                int socket = -1;
                IoInterest interest = IoInterest::Read;
            };

            int m_Epoll = -1;
            int m_Wakeup = -1;
            std::mutex m_Mutex;
            std::unordered_map<std::uint64_t, Watch> m_Watches;
        };
    }

    std::unique_ptr<ReactorPoller> CreateReactorPoller()
    {
        return std::make_unique<EpollReactorPoller>();
    }
}
