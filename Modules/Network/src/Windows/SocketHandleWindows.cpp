#include "Network/SocketHandle.h"

#include "Network/NetworkError.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cstring>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace X_Y
{
    namespace
    {
        std::string SocketErrorText(int error)
        {
            return "Winsock error " + std::to_string(error);
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
            int &length)
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

        class WinsockRuntime
        {
        public:
            WinsockRuntime()
            {
                WSADATA data{};
                const int error = WSAStartup(MAKEWORD(2, 2), &data);
                if (error != 0)
                    ThrowSocketError("WSAStartup", error);
            }

            ~WinsockRuntime() { WSACleanup(); }
        };

        WinsockRuntime &GetWinsockRuntime()
        {
            static WinsockRuntime runtime;
            return runtime;
        }

        class WindowsSocketHandleImpl final : public SocketHandleImpl
        {
        public:
            WindowsSocketHandleImpl(AddressFamily family, SocketType type)
                : m_Family(family), m_Type(type)
            {
                if (family != AddressFamily::IPv4 && family != AddressFamily::IPv6)
                    throw std::invalid_argument("Unsupported socket address family");
                if (type != SocketType::Stream && type != SocketType::Datagram)
                    throw std::invalid_argument("Unsupported socket type");
                (void)GetWinsockRuntime();
                const int nativeFamily =
                    family == AddressFamily::IPv4 ? AF_INET : AF_INET6;
                const int nativeType =
                    type == SocketType::Stream ? SOCK_STREAM : SOCK_DGRAM;
                const int protocol =
                    type == SocketType::Stream ? IPPROTO_TCP : IPPROTO_UDP;
                m_Socket = ::socket(nativeFamily, nativeType, protocol);
                if (m_Socket == INVALID_SOCKET)
                    ThrowSocketError("socket", WSAGetLastError());
            }

            WindowsSocketHandleImpl(
                SOCKET socket,
                AddressFamily family,
                SocketType type) noexcept
                : m_Family(family), m_Type(type), m_Socket(socket)
            {
            }

            ~WindowsSocketHandleImpl() override { Close(); }

            bool Bind(const SocketAddr &addr) override
            {
                if (!IsValid() || addr.family != m_Family)
                    return false;

                sockaddr_storage nativeAddr{};
                int length = 0;
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
                int length = 0;
                ToNativeAddress(addr, nativeAddr, length);
                if (::connect(
                        m_Socket,
                        reinterpret_cast<const sockaddr *>(&nativeAddr),
                        length) == SOCKET_ERROR)
                    ThrowSocketError("connect", WSAGetLastError());
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

                sockaddr_storage peer{};
                int peerLength = sizeof(peer);
                const SOCKET accepted = ::accept(
                    m_Socket,
                    reinterpret_cast<sockaddr *>(&peer),
                    &peerLength);
                if (accepted == INVALID_SOCKET)
                {
                    const int error = WSAGetLastError();
                    if (error == WSAEWOULDBLOCK)
                        return nullptr;
                    ThrowSocketError("accept", error);
                }

                const AddressFamily family =
                    peer.ss_family == AF_INET ? AddressFamily::IPv4 : AddressFamily::IPv6;
                try
                {
                    return std::make_unique<WindowsSocketHandleImpl>(
                        accepted,
                        family,
                        SocketType::Stream);
                }
                catch (...)
                {
                    closesocket(accepted);
                    throw;
                }
            }

            std::size_t SendSome(const void *data, std::size_t size) override
            {
                EnsureValid();
                if (size == 0)
                    return 0;
                if (data == nullptr)
                    throw std::invalid_argument("Socket send data is null");

                const int chunkSize = static_cast<int>(std::min(
                    size,
                    static_cast<std::size_t>(std::numeric_limits<int>::max())));
                for (;;)
                {
                    const int result = ::send(
                        m_Socket,
                        static_cast<const char *>(data),
                        chunkSize,
                        0);
                    if (result != SOCKET_ERROR)
                        return static_cast<std::size_t>(result);

                    const int error = WSAGetLastError();
                    if (error == WSAEINTR)
                        continue;
                    ThrowSocketError("send", error);
                }
            }

            std::size_t ReceiveSome(void *data, std::size_t size) override
            {
                EnsureValid();
                if (size == 0)
                    return 0;
                if (data == nullptr)
                    throw std::invalid_argument("Socket receive buffer is null");

                const int chunkSize = static_cast<int>(std::min(
                    size,
                    static_cast<std::size_t>(std::numeric_limits<int>::max())));
                for (;;)
                {
                    const int result = ::recv(
                        m_Socket,
                        static_cast<char *>(data),
                        chunkSize,
                        0);
                    if (result != SOCKET_ERROR)
                        return static_cast<std::size_t>(result);

                    const int error = WSAGetLastError();
                    if (error == WSAEINTR)
                        continue;
                    ThrowSocketError("recv", error);
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
                if (size > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                    throw std::length_error("Datagram exceeds Winsock send limit");

                sockaddr_storage nativePeer{};
                int peerLength = 0;
                ToNativeAddress(peer, nativePeer, peerLength);
                const int result = ::sendto(
                    m_Socket,
                    static_cast<const char *>(data),
                    static_cast<int>(size),
                    0,
                    reinterpret_cast<const sockaddr *>(&nativePeer),
                    peerLength);
                if (result == SOCKET_ERROR)
                    ThrowSocketError("sendto", WSAGetLastError());
                return static_cast<std::size_t>(result);
            }

            DatagramReceiveResult ReceiveFrom(void *data, std::size_t capacity) override
            {
                EnsureValid();
                if (m_Type != SocketType::Datagram)
                    throw std::logic_error("ReceiveFrom requires a datagram socket");
                if (capacity != 0 && data == nullptr)
                    throw std::invalid_argument("Socket receive buffer is null");
                if (capacity > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                    capacity = static_cast<std::size_t>(std::numeric_limits<int>::max());

                sockaddr_storage peer{};
                int peerLength = sizeof(peer);
                const int result = ::recvfrom(
                    m_Socket,
                    static_cast<char *>(data),
                    static_cast<int>(capacity),
                    0,
                    reinterpret_cast<sockaddr *>(&peer),
                    &peerLength);
                if (result == SOCKET_ERROR)
                {
                    const int error = WSAGetLastError();
                    if (error != WSAEMSGSIZE)
                        ThrowSocketError("recvfrom", error);
                    return {capacity, FromNativeAddress(peer), true};
                }
                return {
                    static_cast<std::size_t>(result),
                    FromNativeAddress(peer),
                    false};
            }

            bool SetNonBlocking(bool enable) override
            {
                if (!IsValid())
                    return false;
                u_long value = enable ? 1UL : 0UL;
                return ioctlsocket(m_Socket, FIONBIO, &value) == 0;
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

                return setsockopt(
                           m_Socket,
                           level,
                           nativeOption,
                           static_cast<const char *>(val),
                           static_cast<int>(optlen)) == 0;
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
                if (m_Socket == INVALID_SOCKET)
                    return;
                closesocket(m_Socket);
                m_Socket = INVALID_SOCKET;
            }

            bool IsValid() const override { return m_Socket != INVALID_SOCKET; }

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

            std::optional<SocketAddr> GetSocketAddress(bool peer) const
            {
                sockaddr_storage nativeAddr{};
                int length = sizeof(nativeAddr);
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
            SOCKET m_Socket = INVALID_SOCKET;
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
        return new WindowsSocketHandleImpl(family, type);
    }
}
