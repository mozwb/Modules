#include "Network/SocketHandle.h"

#include "Network/NetworkError.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdexcept>
#include <string>
#include <system_error>
#include <sys/socket.h>
#include <unistd.h>

namespace X_Y
{
    namespace
    {
        std::string SocketErrorText(int error)
        {
            return std::error_code(error, std::system_category()).message();
        }

        [[noreturn]] void ThrowSocketError(const char *operation, int error)
        {
            throw NetworkError(operation, error, SocketErrorText(error));
        }

        void EnsureAddressFamily(const SocketAddr &addr, AddressFamily family)
        {
            if (addr.family != family)
                throw std::invalid_argument("Socket address family does not match socket");
        }

        void ToNativeAddress(
            const SocketAddr &addr,
            sockaddr_storage &storage,
            socklen_t &length)
        {
            std::memset(&storage, 0, sizeof(storage));
            if (addr.family == AddressFamily::IPv4)
            {
                auto *nativeAddr = reinterpret_cast<sockaddr_in *>(&storage);
                nativeAddr->sin_family = AF_INET;
                nativeAddr->sin_port = htons(addr.port);
                std::memcpy(&nativeAddr->sin_addr, addr.ipBytes, 4);
                length = sizeof(*nativeAddr);
                return;
            }
            if (addr.family == AddressFamily::IPv6)
            {
                auto *nativeAddr = reinterpret_cast<sockaddr_in6 *>(&storage);
                nativeAddr->sin6_family = AF_INET6;
                nativeAddr->sin6_port = htons(addr.port);
                nativeAddr->sin6_scope_id = addr.scopeId;
                std::memcpy(&nativeAddr->sin6_addr, addr.ipBytes, 16);
                length = sizeof(*nativeAddr);
                return;
            }
            throw std::invalid_argument("Unsupported socket address family");
        }

        SocketAddr FromNativeAddress(const sockaddr_storage &storage)
        {
            SocketAddr addr{};
            if (storage.ss_family == AF_INET)
            {
                const auto *native = reinterpret_cast<const sockaddr_in *>(&storage);
                addr.family = AddressFamily::IPv4;
                addr.port = ntohs(native->sin_port);
                std::memcpy(addr.ipBytes, &native->sin_addr, 4);
                return addr;
            }
            if (storage.ss_family == AF_INET6)
            {
                const auto *native = reinterpret_cast<const sockaddr_in6 *>(&storage);
                addr.family = AddressFamily::IPv6;
                addr.port = ntohs(native->sin6_port);
                addr.scopeId = native->sin6_scope_id;
                std::memcpy(addr.ipBytes, &native->sin6_addr, 16);
                return addr;
            }
            throw NetworkError("socket address", 0, "unsupported address family returned");
        }

        class LinuxSocketHandleImpl final : public SocketHandleImpl
        {
        public:
            LinuxSocketHandleImpl(AddressFamily family, SocketType type)
                : m_Family(family), m_Type(type)
            {
                if (family != AddressFamily::IPv4 && family != AddressFamily::IPv6)
                    throw std::invalid_argument("Unsupported socket address family");
                if (type != SocketType::Stream && type != SocketType::Datagram)
                    throw std::invalid_argument("Unsupported socket type");
                const int nativeFamily =
                    family == AddressFamily::IPv4 ? AF_INET : AF_INET6;
                const int nativeType =
                    type == SocketType::Stream ? SOCK_STREAM : SOCK_DGRAM;
                const int protocol =
                    type == SocketType::Stream ? IPPROTO_TCP : IPPROTO_UDP;
                m_Socket = ::socket(nativeFamily, nativeType, protocol);
                if (m_Socket < 0)
                    ThrowSocketError("socket", errno);
            }

            LinuxSocketHandleImpl(
                int socket,
                AddressFamily family,
                SocketType type) noexcept
                : m_Family(family), m_Type(type), m_Socket(socket)
            {
            }

            ~LinuxSocketHandleImpl() override { Close(); }

            bool Bind(const SocketAddr &addr) override
            {
                if (!IsValid() || addr.family != m_Family)
                    return false;

                sockaddr_storage nativeAddr{};
                socklen_t length = 0;
                ToNativeAddress(addr, nativeAddr, length);
                return ::bind(
                           m_Socket,
                           reinterpret_cast<const sockaddr *>(&nativeAddr),
                           length) == 0;
            }

            void Connect(const SocketAddr &addr) override
            {
                EnsureValid();
                EnsureAddressFamily(addr, m_Family);

                sockaddr_storage nativeAddr{};
                socklen_t length = 0;
                ToNativeAddress(addr, nativeAddr, length);
                if (::connect(
                        m_Socket,
                        reinterpret_cast<const sockaddr *>(&nativeAddr),
                        length) != 0)
                    ThrowSocketError("connect", errno);
            }

            bool Listen(int backlog) override
            {
                return IsValid() && m_Type == SocketType::Stream &&
                       backlog > 0 && ::listen(m_Socket, backlog) == 0;
            }

            std::unique_ptr<SocketHandleImpl> Accept() override
            {
                EnsureValid();
                if (m_Type != SocketType::Stream)
                    throw std::logic_error("Accept requires a stream socket");

                for (;;)
                {
                    sockaddr_storage peer{};
                    socklen_t peerLength = sizeof(peer);
                    const int accepted = ::accept(
                        m_Socket,
                        reinterpret_cast<sockaddr *>(&peer),
                        &peerLength);
                    if (accepted < 0)
                    {
                        const int error = errno;
                        if (error == EAGAIN || error == EWOULDBLOCK)
                            return nullptr;
                        if (error == EINTR)
                            continue;
                        ThrowSocketError("accept", error);
                    }

                    const AddressFamily family =
                        peer.ss_family == AF_INET ? AddressFamily::IPv4 : AddressFamily::IPv6;
                    try
                    {
                        return std::make_unique<LinuxSocketHandleImpl>(
                            accepted,
                            family,
                            SocketType::Stream);
                    }
                    catch (...)
                    {
                        ::close(accepted);
                        throw;
                    }
                }
            }

            std::size_t SendSome(const void *data, std::size_t size) override
            {
                EnsureValid();
                if (size == 0)
                    return 0;
                if (data == nullptr)
                    throw std::invalid_argument("Socket send data is null");

                const std::size_t chunkSize = std::min(
                    size,
                    static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
                for (;;)
                {
                    const ssize_t result =
                        ::send(m_Socket, data, chunkSize, MSG_NOSIGNAL);
                    if (result >= 0)
                        return static_cast<std::size_t>(result);

                    const int error = errno;
                    if (error == EINTR)
                        continue;
                    ThrowSocketError("send", error);
                }
            }

            std::size_t SendTo(
                const SocketAddr &peer,
                const void *data,
                std::size_t size) override
            {
                EnsureValid();
                if (m_Type != SocketType::Datagram)
                    throw std::logic_error("SendTo requires a datagram socket");
                EnsureAddressFamily(peer, m_Family);
                if (size != 0 && data == nullptr)
                    throw std::invalid_argument("Socket send data is null");

                sockaddr_storage nativePeer{};
                socklen_t peerLength = 0;
                ToNativeAddress(peer, nativePeer, peerLength);
                for (;;)
                {
                    const ssize_t result = ::sendto(
                        m_Socket,
                        data,
                        size,
                        0,
                        reinterpret_cast<const sockaddr *>(&nativePeer),
                        peerLength);
                    if (result >= 0)
                        return static_cast<std::size_t>(result);
                    const int error = errno;
                    if (error == EINTR)
                        continue;
                    ThrowSocketError("sendto", error);
                }
            }

            DatagramReceiveResult ReceiveFrom(void *data, std::size_t capacity) override
            {
                EnsureValid();
                if (m_Type != SocketType::Datagram)
                    throw std::logic_error("ReceiveFrom requires a datagram socket");
                if (capacity != 0 && data == nullptr)
                    throw std::invalid_argument("Socket receive buffer is null");

                sockaddr_storage peer{};
                socklen_t peerLength = sizeof(peer);
                for (;;)
                {
                    const ssize_t result = ::recvfrom(
                        m_Socket,
                        data,
                        capacity,
                        MSG_TRUNC,
                        reinterpret_cast<sockaddr *>(&peer),
                        &peerLength);
                    if (result >= 0)
                    {
                        const std::size_t datagramSize = static_cast<std::size_t>(result);
                        return {
                            std::min(datagramSize, capacity),
                            FromNativeAddress(peer),
                            datagramSize > capacity};
                    }
                    const int error = errno;
                    if (error == EINTR)
                        continue;
                    ThrowSocketError("recvfrom", error);
                }
            }

            std::size_t ReceiveSome(void *data, std::size_t size) override
            {
                EnsureValid();
                if (size == 0)
                    return 0;
                if (data == nullptr)
                    throw std::invalid_argument("Socket receive buffer is null");

                const std::size_t chunkSize = std::min(
                    size,
                    static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
                for (;;)
                {
                    const ssize_t result = ::recv(m_Socket, data, chunkSize, 0);
                    if (result >= 0)
                        return static_cast<std::size_t>(result);

                    const int error = errno;
                    if (error == EINTR)
                        continue;
                    ThrowSocketError("recv", error);
                }
            }

            bool SetNonBlocking(bool enable) override
            {
                if (!IsValid())
                    return false;

                const int flags = fcntl(m_Socket, F_GETFL, 0);
                if (flags < 0)
                    return false;
                const int updatedFlags =
                    enable ? flags | O_NONBLOCK : flags & ~O_NONBLOCK;
                return fcntl(m_Socket, F_SETFL, updatedFlags) == 0;
            }

            bool SetOption(SocketOptionId option, const void *val, SocketLen optlen) override
            {
                if (!IsValid() || val == nullptr || optlen != sizeof(int))
                    return false;

                int level = SOL_SOCKET;
                int nativeOption = 0;
                switch (option)
                {
                case SocketOptionId::ReuseAddr:
                    nativeOption = SO_REUSEADDR;
                    break;
                case SocketOptionId::TcpNoDelay:
                    level = IPPROTO_TCP;
                    nativeOption = TCP_NODELAY;
                    break;
                case SocketOptionId::RecvBuf:
                    nativeOption = SO_RCVBUF;
                    break;
                case SocketOptionId::SendBuf:
                    nativeOption = SO_SNDBUF;
                    break;
                default:
                    return false;
                }

                return setsockopt(m_Socket, level, nativeOption, val, optlen) == 0;
            }

            std::optional<SocketAddr> GetLocalAddr() const override
            {
                if (!IsValid())
                    return std::nullopt;

                return GetSocketAddress(false);
            }

            std::optional<SocketAddr> GetPeerAddr() const override
            {
                if (!IsValid())
                    return std::nullopt;
                return GetSocketAddress(true);
            }

            void Close() noexcept override
            {
                if (m_Socket < 0)
                    return;
                ::close(m_Socket);
                m_Socket = -1;
            }

            bool IsValid() const override { return m_Socket >= 0; }

            std::uintptr_t GetNativeHandle() const override
            {
                return IsValid()
                           ? static_cast<std::uintptr_t>(m_Socket)
                           : InvalidNativeSocketHandle;
            }

        private:
            void EnsureValid() const
            {
                if (!IsValid())
                    throw std::logic_error("Socket handle is closed");
            }

            void EnsureValid() const
            {
                if (!IsValid())
                    throw std::logic_error("Socket handle is closed");
            }

            std::optional<SocketAddr> GetSocketAddress(bool peer) const
            {
                sockaddr_storage nativeAddr{};
                socklen_t length = sizeof(nativeAddr);
                const int result = peer
                                       ? getpeername(
                                             m_Socket,
                                             reinterpret_cast<sockaddr *>(&nativeAddr),
                                             &length)
                                       : getsockname(
                                             m_Socket,
                                             reinterpret_cast<sockaddr *>(&nativeAddr),
                                             &length);
                if (result != 0)
                    return std::nullopt;

                try
                {
                    return FromNativeAddress(nativeAddr);
                }
                catch (const NetworkError &)
                {
                    return std::nullopt;
                }
            }

            AddressFamily m_Family;
            SocketType m_Type;
            int m_Socket = -1;
        };
    }

    SocketHandle::SocketHandle(AddressFamily family, SocketType type)
        : m_Impl(CreateSocketHandleImpl(family, type))
    {
        if (!m_Impl)
            throw std::runtime_error("Socket handle factory returned no implementation");
    }

    SocketHandleImpl *CreateSocketHandleImpl(AddressFamily family, SocketType type)
    {
        return new LinuxSocketHandleImpl(family, type);
    }
}
