#include "Network/src/ReactorPoller.h"
#include "Network/NetworkError.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <array>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace X_Y::detail
{
    namespace
    {
        [[noreturn]] void ThrowWinsockError(const char *operation, int error)
        {
            throw NetworkError(
                operation,
                error,
                "Winsock error " + std::to_string(error));
        }

        class WinsockSession
        {
        public:
            WinsockSession()
            {
                WSADATA data{};
                const int error = WSAStartup(MAKEWORD(2, 2), &data);
                if (error != 0)
                    ThrowWinsockError("WSAStartup", error);
            }

            ~WinsockSession()
            {
                WSACleanup();
            }
        };

        class WakeupPair
        {
        public:
            WakeupPair()
            {
                m_Receiver = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                if (m_Receiver == INVALID_SOCKET)
                    ThrowWinsockError("socket", WSAGetLastError());

                sockaddr_in address{};
                address.sin_family = AF_INET;
                address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                address.sin_port = 0;
                if (::bind(
                        m_Receiver,
                        reinterpret_cast<const sockaddr *>(&address),
                        sizeof(address)) == SOCKET_ERROR)
                    Fail("bind");

                int addressLength = sizeof(address);
                if (getsockname(
                        m_Receiver,
                        reinterpret_cast<sockaddr *>(&address),
                        &addressLength) == SOCKET_ERROR)
                    Fail("getsockname");

                m_Sender = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                if (m_Sender == INVALID_SOCKET)
                    Fail("socket");
                if (::connect(
                        m_Sender,
                        reinterpret_cast<const sockaddr *>(&address),
                        sizeof(address)) == SOCKET_ERROR)
                    Fail("connect");

                u_long nonBlocking = 1;
                if (ioctlsocket(m_Receiver, FIONBIO, &nonBlocking) == SOCKET_ERROR ||
                    ioctlsocket(m_Sender, FIONBIO, &nonBlocking) == SOCKET_ERROR)
                    Fail("ioctlsocket");
            }

            ~WakeupPair()
            {
                if (m_Sender != INVALID_SOCKET)
                    closesocket(m_Sender);
                if (m_Receiver != INVALID_SOCKET)
                    closesocket(m_Receiver);
            }

            SOCKET Receiver() const noexcept { return m_Receiver; }

            void Wake()
            {
                constexpr char signal = 1;
                const int result = ::send(m_Sender, &signal, sizeof(signal), 0);
                if (result == SOCKET_ERROR)
                {
                    const int error = WSAGetLastError();
                    if (error == WSAEWOULDBLOCK)
                        return;
                    ThrowWinsockError("send wakeup", error);
                }
            }

            void Drain() noexcept
            {
                std::array<char, 64> buffer{};
                for (;;)
                {
                    const int result = ::recv(
                        m_Receiver,
                        buffer.data(),
                        static_cast<int>(buffer.size()),
                        0);
                    if (result != SOCKET_ERROR)
                        continue;
                    const int error = WSAGetLastError();
                    if (error == WSAEINTR)
                        continue;
                    return;
                }
            }

        private:
            [[noreturn]] void Fail(const char *operation)
            {
                const int error = WSAGetLastError();
                if (m_Sender != INVALID_SOCKET)
                {
                    closesocket(m_Sender);
                    m_Sender = INVALID_SOCKET;
                }
                if (m_Receiver != INVALID_SOCKET)
                {
                    closesocket(m_Receiver);
                    m_Receiver = INVALID_SOCKET;
                }
                ThrowWinsockError(operation, error);
            }

            SOCKET m_Receiver = INVALID_SOCKET;
            SOCKET m_Sender = INVALID_SOCKET;
        };

        struct Watch
        {
            SOCKET socket = INVALID_SOCKET;
            IoInterest interest = IoInterest::Read;
        };

        class WsapollReactorPoller final : public ReactorPoller
        {
        public:
            WsapollReactorPoller()
                : m_Winsock(std::make_unique<WinsockSession>()),
                  m_Wakeup(std::make_unique<WakeupPair>())
            {
            }

            void Add(
                std::uint64_t id,
                std::uintptr_t nativeSocket,
                IoInterest interest) override
            {
                if (nativeSocket == InvalidNativeSocketHandle)
                    throw std::invalid_argument("Cannot poll an invalid socket");
                if (nativeSocket > std::numeric_limits<SOCKET>::max())
                    throw std::out_of_range("Native socket handle exceeds Winsock range");

                {
                    std::lock_guard lock(m_Mutex);
                    if (!m_Watches.emplace(
                            id,
                            Watch{static_cast<SOCKET>(nativeSocket), interest})
                             .second)
                        throw std::logic_error("Duplicate Reactor registration id");
                }
                try
                {
                    m_Wakeup->Wake();
                }
                catch (...)
                {
                    std::lock_guard lock(m_Mutex);
                    m_Watches.erase(id);
                    throw;
                }
            }

            bool Update(std::uint64_t id, IoInterest interest) override
            {
                IoInterest previous = IoInterest::Read;
                {
                    std::lock_guard lock(m_Mutex);
                    const auto found = m_Watches.find(id);
                    if (found == m_Watches.end())
                        return false;
                    previous = found->second.interest;
                    found->second.interest = interest;
                }
                try
                {
                    m_Wakeup->Wake();
                }
                catch (...)
                {
                    std::lock_guard lock(m_Mutex);
                    const auto found = m_Watches.find(id);
                    if (found != m_Watches.end() && found->second.interest == interest)
                        found->second.interest = previous;
                    throw;
                }
                return true;
            }

            void Remove(std::uint64_t id) noexcept override
            {
                {
                    std::lock_guard lock(m_Mutex);
                    m_Watches.erase(id);
                }
                try
                {
                    m_Wakeup->Wake();
                }
                catch (...)
                {
                    // Wait() has a bounded timeout, so removal still takes effect.
                }
            }

            std::vector<PollerEvent> Wait() override
            {
                std::vector<std::pair<std::uint64_t, Watch>> snapshot;
                {
                    std::lock_guard lock(m_Mutex);
                    snapshot.reserve(m_Watches.size());
                    for (const auto &entry : m_Watches)
                        snapshot.push_back(entry);
                }

                if (snapshot.size() >= std::numeric_limits<ULONG>::max())
                    throw std::length_error("Too many sockets registered with WSAPoll");

                std::vector<WSAPOLLFD> descriptors(snapshot.size() + 1);
                descriptors[0].fd = m_Wakeup->Receiver();
                descriptors[0].events = POLLRDNORM;
                descriptors[0].revents = 0;

                for (std::size_t index = 0; index < snapshot.size(); ++index)
                {
                    auto &descriptor = descriptors[index + 1];
                    descriptor.fd = snapshot[index].second.socket;
                    descriptor.events = ToPollEvents(snapshot[index].second.interest);
                    descriptor.revents = 0;
                }

                const int result = WSAPoll(
                    descriptors.data(),
                    static_cast<ULONG>(descriptors.size()),
                    // Bound the wait so Stop still takes effect if the wakeup send fails.
                    100);
                if (result == SOCKET_ERROR)
                {
                    const int error = WSAGetLastError();
                    if (error == WSAEINTR)
                        return {};
                    ThrowWinsockError("WSAPoll", error);
                }

                std::vector<PollerEvent> events;
                events.reserve(static_cast<std::size_t>(result));
                if (descriptors[0].revents != 0)
                    m_Wakeup->Drain();

                for (std::size_t index = 0; index < snapshot.size(); ++index)
                {
                    const SHORT ready = descriptors[index + 1].revents;
                    if (ready == 0)
                        continue;
                    events.push_back({snapshot[index].first, FromPollEvents(ready)});
                }
                return events;
            }

            void Wake() override
            {
                m_Wakeup->Wake();
            }

        private:
            static SHORT ToPollEvents(IoInterest interest) noexcept
            {
                SHORT events = 0;
                if (interest == IoInterest::Read || interest == IoInterest::ReadWrite)
                    events |= POLLRDNORM;
                if (interest == IoInterest::Write || interest == IoInterest::ReadWrite)
                    events |= POLLWRNORM;
                return events;
            }

            static IoReady FromPollEvents(SHORT events) noexcept
            {
                std::uint8_t ready = 0;
                if ((events & (POLLRDNORM | POLLRDBAND)) != 0)
                    ready |= static_cast<std::uint8_t>(IoReady::Read);
                if ((events & (POLLWRNORM | POLLWRBAND)) != 0)
                    ready |= static_cast<std::uint8_t>(IoReady::Write);
                if ((events & POLLERR) != 0)
                    ready |= static_cast<std::uint8_t>(IoReady::Error);
                if ((events & (POLLHUP | POLLNVAL)) != 0)
                    ready |= static_cast<std::uint8_t>(IoReady::Closed);
                return static_cast<IoReady>(ready);
            }

            std::unique_ptr<WinsockSession> m_Winsock;
            std::unique_ptr<WakeupPair> m_Wakeup;
            std::mutex m_Mutex;
            std::unordered_map<std::uint64_t, Watch> m_Watches;
        };
    }

    std::unique_ptr<ReactorPoller> CreateReactorPoller()
    {
        return std::make_unique<WsapollReactorPoller>();
    }
}
