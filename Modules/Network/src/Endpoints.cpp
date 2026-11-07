#include "Network/Endpoints.h"

#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <system_error>

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

namespace X_Y
{
    namespace
    {
        std::string SocketErrorText(int error)
        {
#ifdef _WIN32
            return "Winsock error " + std::to_string(error);
#else
            return std::error_code(error, std::system_category()).message();
#endif
        }

        [[noreturn]] void ThrowSocketError(const char *operation, int error)
        {
            throw NetworkError(operation, error, SocketErrorText(error));
        }

        void ValidateTimeout(
            SocketTimeoutDirection direction,
            std::chrono::milliseconds timeout)
        {
            if (direction != SocketTimeoutDirection::Send &&
                direction != SocketTimeoutDirection::Receive)
                throw std::invalid_argument("Unsupported socket timeout direction");
            if (timeout.count() < 0)
                throw std::invalid_argument("Socket timeout must not be negative");
            if (static_cast<std::uint64_t>(timeout.count()) >
                std::numeric_limits<std::uint32_t>::max())
                throw std::out_of_range("Socket timeout exceeds the supported range");
        }

        void ApplyTimeout(
            SocketHandle &socket,
            SocketTimeoutDirection direction,
            std::chrono::milliseconds timeout)
        {
            if (!socket.SetTimeout(direction, timeout))
                throw NetworkError(
                    "setsockopt",
                    0,
                    "failed to apply socket timeout");
        }

#ifdef _WIN32
        class WinsockSession
        {
        public:
            WinsockSession()
            {
                WSADATA data{};
                const int error = WSAStartup(MAKEWORD(2, 2), &data);
                if (error != 0)
                    ThrowSocketError("WSAStartup", error);
            }

            ~WinsockSession() { WSACleanup(); }
        };
#endif

        std::optional<SocketAddr> ConvertAddress(const sockaddr *native)
        {
            if (native == nullptr)
                return std::nullopt;

            SocketAddr address{};
            if (native->sa_family == AF_INET)
            {
                const auto *ipv4 = reinterpret_cast<const sockaddr_in *>(native);
                address.family = AddressFamily::IPv4;
                address.port = ntohs(ipv4->sin_port);
                std::memcpy(address.ipBytes, &ipv4->sin_addr, 4);
                return address;
            }
            if (native->sa_family == AF_INET6)
            {
                const auto *ipv6 = reinterpret_cast<const sockaddr_in6 *>(native);
                address.family = AddressFamily::IPv6;
                address.port = ntohs(ipv6->sin6_port);
                address.scopeId = ipv6->sin6_scope_id;
                std::memcpy(address.ipBytes, &ipv6->sin6_addr, 16);
                return address;
            }
            return std::nullopt;
        }

        class SocketStreamTransport final : public StreamTransport
        {
        public:
            SocketStreamTransport() = default;
            explicit SocketStreamTransport(SocketHandle socket, bool nonBlocking = false)
                : m_Socket(std::move(socket)), m_NonBlocking(nonBlocking)
            {
            }

            void Connect(const std::string &host, std::uint16_t port) override
            {
                if (host.empty())
                    throw std::invalid_argument("Stream host must not be empty");
                if (port == 0)
                    throw std::invalid_argument("Stream port must not be zero");

                Close();
                m_NonBlocking = false;
#ifdef _WIN32
                WinsockSession winsock;
#endif

                const std::string service = std::to_string(port);
                addrinfo hints{};
                hints.ai_family = AF_UNSPEC;
                hints.ai_socktype = SOCK_STREAM;
                hints.ai_protocol = IPPROTO_TCP;

                addrinfo *rawAddresses = nullptr;
                const int resolveError =
                    getaddrinfo(host.c_str(), service.c_str(), &hints, &rawAddresses);
                if (resolveError != 0)
                {
#ifdef _WIN32
                    ThrowSocketError("getaddrinfo", resolveError);
#else
                    throw NetworkError(
                        "getaddrinfo",
                        resolveError,
                        gai_strerror(resolveError));
#endif
                }
                const std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addresses(
                    rawAddresses,
                    &freeaddrinfo);

                std::optional<NetworkError> lastError;
                for (const addrinfo *entry = addresses.get(); entry != nullptr; entry = entry->ai_next)
                {
                    const auto address = ConvertAddress(entry->ai_addr);
                    if (!address)
                        continue;

                    try
                    {
                        SocketHandle socket(address->family, SocketType::Stream);
                        if (m_SendTimeout.count() != 0)
                            ApplyTimeout(socket, SocketTimeoutDirection::Send, m_SendTimeout);
                        if (m_ReceiveTimeout.count() != 0)
                            ApplyTimeout(socket, SocketTimeoutDirection::Receive, m_ReceiveTimeout);
                        socket.Connect(*address);
                        m_Socket.emplace(std::move(socket));
                        return;
                    }
                    catch (const NetworkError &error)
                    {
                        lastError.emplace(error);
                    }
                }

                if (lastError)
                    throw *lastError;
                throw NetworkError("connect", 0, "no usable address was returned");
            }

            bool SetTimeout(
                SocketTimeoutDirection direction,
                std::chrono::milliseconds timeout) override
            {
                ValidateTimeout(direction, timeout);
                if (m_Socket && !m_Socket->SetTimeout(direction, timeout))
                    return false;

                if (direction == SocketTimeoutDirection::Send)
                    m_SendTimeout = timeout;
                else
                    m_ReceiveTimeout = timeout;
                return true;
            }

            std::size_t SendSome(const void *data, std::size_t size) override
            {
                EnsureBlockingMode();
                return RequireSocket().SendSome(data, size);
            }

            std::size_t ReceiveSome(void *data, std::size_t capacity) override
            {
                EnsureBlockingMode();
                return RequireSocket().ReceiveSome(data, capacity);
            }

            SocketIoResult<std::size_t> TrySendSome(
                const void *data,
                std::size_t size) override
            {
                EnsureNonBlockingSocket();
                return RequireSocket().TrySendSome(data, size);
            }

            SocketIoResult<std::size_t> TryReceiveSome(
                void *data,
                std::size_t capacity) override
            {
                EnsureNonBlockingSocket();
                return RequireSocket().TryReceiveSome(data, capacity);
            }

            IoContext::Registration Watch(
                IoContext &context,
                IoInterest interest,
                IoContext::Handler handler) override
            {
                EnsureNonBlockingSocket();
                return context.Watch(RequireSocket(), interest, std::move(handler));
            }

            void Close() noexcept override
            {
                if (m_Socket)
                {
                    m_Socket->Close();
                    m_Socket.reset();
                }
            }

            bool IsConnected() const noexcept override
            {
                return m_Socket && m_Socket->IsValid();
            }

            std::optional<SocketAddr> GetLocalAddr() const override
            {
                return m_Socket ? m_Socket->GetLocalAddr() : std::nullopt;
            }

            std::optional<SocketAddr> GetPeerAddr() const override
            {
                return m_Socket ? m_Socket->GetPeerAddr() : std::nullopt;
            }

        private:
            void EnsureNonBlockingSocket()
            {
                auto &socket = RequireSocket();
                if (!m_NonBlocking && !socket.SetNonBlocking(true))
                    throw NetworkError(
                        "set non-blocking",
                        0,
                        "failed to enable non-blocking mode for stream endpoint");
                m_NonBlocking = true;
            }

            void EnsureBlockingMode() const
            {
                if (m_NonBlocking)
                    throw std::logic_error(
                        "Blocking stream operations cannot be used after Reactor mode is enabled");
            }

            SocketHandle &RequireSocket()
            {
                if (!m_Socket || !m_Socket->IsValid())
                    throw std::logic_error("Stream transport is not connected");
                return *m_Socket;
            }

            std::optional<SocketHandle> m_Socket;
            bool m_NonBlocking = false;
            std::chrono::milliseconds m_SendTimeout{0};
            std::chrono::milliseconds m_ReceiveTimeout{0};
        };

        class SocketStreamServerTransport final : public StreamServerTransport
        {
        public:
            bool Listen(const SocketAddr &address, int backlog) override
            {
                Close();
                m_NonBlocking = false;
                if (backlog <= 0)
                    return false;

                SocketHandle socket(address.family, SocketType::Stream);
                const int reuseAddress = 1;
                if (!socket.SetOption(
                        SocketOptionId::ReuseAddr,
                        &reuseAddress,
                        sizeof(reuseAddress)) ||
                    !socket.Bind(address) ||
                    !socket.Listen(backlog))
                    return false;

                m_Socket.emplace(std::move(socket));
                return true;
            }

            bool SetTimeout(
                SocketTimeoutDirection direction,
                std::chrono::milliseconds timeout) override
            {
                ValidateTimeout(direction, timeout);
                if (direction == SocketTimeoutDirection::Send)
                    m_SendTimeout = timeout;
                else
                    m_ReceiveTimeout = timeout;
                return true;
            }

            std::unique_ptr<StreamTransport> Accept() override
            {
                if (!m_Socket || !m_Socket->IsValid())
                    throw std::logic_error("Stream server is not listening");
                EnsureBlockingMode();

                auto accepted = m_Socket->Accept();
                if (!accepted)
                    return nullptr;
                auto connection =
                    std::make_unique<SocketStreamTransport>(std::move(*accepted));
                if (m_SendTimeout.count() != 0 &&
                    !connection->SetTimeout(SocketTimeoutDirection::Send, m_SendTimeout))
                    throw NetworkError("setsockopt", 0, "failed to configure accepted socket");
                if (m_ReceiveTimeout.count() != 0 &&
                    !connection->SetTimeout(
                        SocketTimeoutDirection::Receive,
                        m_ReceiveTimeout))
                    throw NetworkError("setsockopt", 0, "failed to configure accepted socket");
                return connection;
            }

            SocketIoResult<std::unique_ptr<StreamTransport>> TryAccept() override
            {
                if (!m_Socket || !m_Socket->IsValid())
                    throw std::logic_error("Stream server is not listening");
                EnsureNonBlockingSocket();

                auto accepted = m_Socket->TryAccept();
                if (accepted.status != SocketIoStatus::Completed)
                    return {accepted.status, std::nullopt};
                if (!accepted.value || !accepted.value->IsValid())
                    throw std::logic_error("Socket implementation accepted without a socket");

                auto connection = std::make_unique<SocketStreamTransport>(
                    std::move(*accepted.value),
                    true);
                if (m_SendTimeout.count() != 0 &&
                    !connection->SetTimeout(SocketTimeoutDirection::Send, m_SendTimeout))
                    throw NetworkError(
                        "setsockopt",
                        0,
                        "failed to configure accepted socket");
                if (m_ReceiveTimeout.count() != 0 &&
                    !connection->SetTimeout(
                        SocketTimeoutDirection::Receive,
                        m_ReceiveTimeout))
                    throw NetworkError(
                        "setsockopt",
                        0,
                        "failed to configure accepted socket");
                return {
                    SocketIoStatus::Completed,
                    std::unique_ptr<StreamTransport>(std::move(connection))};
            }

            void Close() noexcept override
            {
                if (m_Socket)
                {
                    m_Socket->Close();
                    m_Socket.reset();
                }
            }

            IoContext::Registration Watch(
                IoContext &context,
                IoInterest interest,
                IoContext::Handler handler) override
            {
                if (!m_Socket || !m_Socket->IsValid())
                    throw std::logic_error("Stream server is not listening");
                EnsureNonBlockingSocket();
                return context.Watch(*m_Socket, interest, std::move(handler));
            }

            bool IsListening() const noexcept override
            {
                return m_Socket && m_Socket->IsValid();
            }

            std::optional<SocketAddr> GetLocalAddr() const override
            {
                return m_Socket ? m_Socket->GetLocalAddr() : std::nullopt;
            }

        private:
            void EnsureNonBlockingSocket()
            {
                if (!m_NonBlocking && !m_Socket->SetNonBlocking(true))
                    throw NetworkError(
                        "set non-blocking",
                        0,
                        "failed to enable non-blocking mode for stream server");
                m_NonBlocking = true;
            }

            void EnsureBlockingMode() const
            {
                if (m_NonBlocking)
                    throw std::logic_error(
                        "Blocking accept cannot be used after Reactor mode is enabled");
            }

            std::optional<SocketHandle> m_Socket;
            bool m_NonBlocking = false;
            std::chrono::milliseconds m_SendTimeout{0};
            std::chrono::milliseconds m_ReceiveTimeout{0};
        };

        class SocketDatagramTransport final : public DatagramTransport
        {
        public:
            explicit SocketDatagramTransport(AddressFamily family)
                : m_Socket(family, SocketType::Datagram)
            {
            }

            bool Bind(const SocketAddr &address) override
            {
                EnsureBlockingMode();
                const bool bound = m_Socket.Bind(address);
                if (bound)
                    m_IsBound = true;
                return bound;
            }

            bool SetTimeout(
                SocketTimeoutDirection direction,
                std::chrono::milliseconds timeout) override
            {
                ValidateTimeout(direction, timeout);
                return m_Socket.SetTimeout(direction, timeout);
            }

            std::size_t SendTo(
                const SocketAddr &peer,
                const void *data,
                std::size_t size) override
            {
                EnsureBlockingMode();
                return m_Socket.SendTo(peer, data, size);
            }

            SocketIoResult<std::size_t> TrySendTo(
                const SocketAddr &peer,
                const void *data,
                std::size_t size) override
            {
                EnsureNonBlockingSocket();
                return m_Socket.TrySendTo(peer, data, size);
            }

            DatagramReceiveResult ReceiveFrom(void *data, std::size_t capacity) override
            {
                EnsureBlockingMode();
                return m_Socket.ReceiveFrom(data, capacity);
            }

            SocketIoResult<DatagramReceiveResult> TryReceiveFrom(
                void *data,
                std::size_t capacity) override
            {
                EnsureNonBlockingSocket();
                return m_Socket.TryReceiveFrom(data, capacity);
            }

            IoContext::Registration Watch(
                IoContext &context,
                IoInterest interest,
                IoContext::Handler handler) override
            {
                EnsureNonBlockingSocket();
                return context.Watch(m_Socket, interest, std::move(handler));
            }

            void Close() noexcept override
            {
                m_Socket.Close();
                m_IsBound = false;
            }

            bool IsValid() const noexcept override { return m_Socket.IsValid(); }
            bool IsBound() const noexcept override { return m_IsBound && IsValid(); }

            std::optional<SocketAddr> GetLocalAddr() const override
            {
                return m_Socket.GetLocalAddr();
            }

        private:
            void EnsureNonBlockingSocket()
            {
                if (!m_NonBlocking && !m_Socket.SetNonBlocking(true))
                    throw NetworkError(
                        "set non-blocking",
                        0,
                        "failed to enable non-blocking mode for datagram endpoint");
                m_NonBlocking = true;
            }

            void EnsureBlockingMode() const
            {
                if (m_NonBlocking)
                    throw std::logic_error(
                        "Blocking datagram operations cannot be used after Reactor mode is enabled");
            }

            SocketHandle m_Socket;
            bool m_IsBound = false;
            bool m_NonBlocking = false;
        };

        void EnsureStreamTransport(const std::unique_ptr<StreamTransport> &transport)
        {
            if (!transport)
                throw std::logic_error("Stream transport is not configured");
        }

        void EnsureBufferCapacity(const Buffer &buffer)
        {
            if (buffer.Capacity != 0 && buffer.Data == nullptr)
                throw std::invalid_argument("Stream receive buffer has no allocated storage");
        }

        void EnsureBlockingEndpointMode(bool reactorMode, const char *endpoint)
        {
            if (reactorMode)
                throw std::logic_error(
                    std::string("Blocking operations cannot be used after Reactor mode is enabled for ") +
                    endpoint);
        }

        void SendAllBytes(StreamTransport &transport, const void *data, std::size_t size)
        {
            if (size != 0 && data == nullptr)
                throw std::invalid_argument("Stream send data is null");

            std::size_t sent = 0;
            while (sent < size)
            {
                const std::size_t count = transport.SendSome(
                    static_cast<const std::byte *>(data) + sent,
                    size - sent);
                if (count == 0)
                    throw NetworkError("send", 0, "socket accepted no data");
                if (count > size - sent)
                    throw std::logic_error("Stream transport reported sending too many bytes");
                sent += count;
            }
        }

        std::uint64_t ReceiveBuffer(StreamTransport &transport, Buffer &buffer)
        {
            EnsureBufferCapacity(buffer);
            if (buffer.Capacity == 0)
            {
                buffer.Size = 0;
                return 0;
            }
            if (buffer.Capacity > std::numeric_limits<std::size_t>::max())
                throw std::length_error("Stream receive buffer is too large");

            const std::size_t capacity = static_cast<std::size_t>(buffer.Capacity);
            const std::size_t received = transport.ReceiveSome(buffer.Data, capacity);
            if (received > capacity)
                throw std::logic_error("Stream transport reported receiving too many bytes");
            buffer.Size = received;
            return buffer.Size;
        }
    }

    std::unique_ptr<StreamTransport> CreateDefaultStreamTransport()
    {
        return std::make_unique<SocketStreamTransport>();
    }

    std::unique_ptr<StreamServerTransport> CreateDefaultStreamServerTransport()
    {
        return std::make_unique<SocketStreamServerTransport>();
    }

    std::unique_ptr<DatagramTransport> CreateDefaultDatagramTransport(AddressFamily family)
    {
        return std::make_unique<SocketDatagramTransport>(family);
    }

    std::size_t StreamConnection::SendSome(const void *data, std::size_t size)
    {
        EnsureBlockingEndpointMode(m_ReactorMode, "stream connection");
        if (!m_Transport || !m_Transport->IsConnected())
            throw std::logic_error("Stream connection is closed");
        return m_Transport->SendSome(data, size);
    }

    std::size_t StreamConnection::ReceiveSome(void *data, std::size_t capacity)
    {
        EnsureBlockingEndpointMode(m_ReactorMode, "stream connection");
        if (!m_Transport || !m_Transport->IsConnected())
            throw std::logic_error("Stream connection is closed");
        return m_Transport->ReceiveSome(data, capacity);
    }

    SocketIoResult<std::size_t> StreamConnection::TrySendSome(
        const void *data,
        std::size_t size)
    {
        if (!m_Transport || !m_Transport->IsConnected())
            throw std::logic_error("Stream connection is closed");
        m_ReactorMode = true;
        return m_Transport->TrySendSome(data, size);
    }

    SocketIoResult<std::size_t> StreamConnection::TryReceiveSome(
        void *data,
        std::size_t capacity)
    {
        if (!m_Transport || !m_Transport->IsConnected())
            throw std::logic_error("Stream connection is closed");
        m_ReactorMode = true;
        return m_Transport->TryReceiveSome(data, capacity);
    }

    void StreamConnection::Watch(
        IoContext &context,
        IoInterest interest,
        IoContext::Handler handler)
    {
        if (!m_Transport || !m_Transport->IsConnected())
            throw std::logic_error("Stream connection is closed");
        m_Registration.Reset();
        m_Registration = m_Transport->Watch(context, interest, std::move(handler));
        m_ReactorMode = true;
    }

    bool StreamConnection::SetTimeout(
        SocketTimeoutDirection direction,
        std::chrono::milliseconds timeout)
    {
        ValidateTimeout(direction, timeout);
        return m_Transport && m_Transport->SetTimeout(direction, timeout);
    }

    void StreamConnection::SendAll(const void *data, std::size_t size)
    {
        EnsureBlockingEndpointMode(m_ReactorMode, "stream connection");
        if (!m_Transport || !m_Transport->IsConnected())
            throw std::logic_error("Stream connection is closed");
        SendAllBytes(*m_Transport, data, size);
    }

    void StreamConnection::SendAll(const Buffer &buffer)
    {
        if (buffer.Size > std::numeric_limits<std::size_t>::max())
            throw std::length_error("Stream send buffer is too large");
        SendAll(buffer.Data, static_cast<std::size_t>(buffer.Size));
    }

    std::uint64_t StreamConnection::ReceiveSome(Buffer &buffer)
    {
        EnsureBlockingEndpointMode(m_ReactorMode, "stream connection");
        if (!m_Transport || !m_Transport->IsConnected())
            throw std::logic_error("Stream connection is closed");
        return ReceiveBuffer(*m_Transport, buffer);
    }

    StreamClient::StreamClient()
        : m_Transport(CreateDefaultStreamTransport())
    {
    }

    StreamClient::StreamClient(std::unique_ptr<StreamTransport> transport)
        : m_Transport(std::move(transport))
    {
        EnsureStreamTransport(m_Transport);
    }

    void StreamClient::Connect(const std::string &host, std::uint16_t port)
    {
        EnsureStreamTransport(m_Transport);
        m_Registration.Reset();
        m_ReactorMode = false;
        m_Transport->Connect(host, port);
    }

    bool StreamClient::SetTimeout(
        SocketTimeoutDirection direction,
        std::chrono::milliseconds timeout)
    {
        ValidateTimeout(direction, timeout);
        EnsureStreamTransport(m_Transport);
        return m_Transport->SetTimeout(direction, timeout);
    }

    std::size_t StreamClient::SendSome(const void *data, std::size_t size)
    {
        EnsureBlockingEndpointMode(m_ReactorMode, "stream client");
        EnsureStreamTransport(m_Transport);
        if (!m_Transport->IsConnected())
            throw std::logic_error("Stream client is not connected");
        return m_Transport->SendSome(data, size);
    }

    std::size_t StreamClient::ReceiveSome(void *data, std::size_t capacity)
    {
        EnsureBlockingEndpointMode(m_ReactorMode, "stream client");
        EnsureStreamTransport(m_Transport);
        if (!m_Transport->IsConnected())
            throw std::logic_error("Stream client is not connected");
        return m_Transport->ReceiveSome(data, capacity);
    }

    SocketIoResult<std::size_t> StreamClient::TrySendSome(
        const void *data,
        std::size_t size)
    {
        EnsureStreamTransport(m_Transport);
        if (!m_Transport->IsConnected())
            throw std::logic_error("Stream client is not connected");
        m_ReactorMode = true;
        return m_Transport->TrySendSome(data, size);
    }

    SocketIoResult<std::size_t> StreamClient::TryReceiveSome(
        void *data,
        std::size_t capacity)
    {
        EnsureStreamTransport(m_Transport);
        if (!m_Transport->IsConnected())
            throw std::logic_error("Stream client is not connected");
        m_ReactorMode = true;
        return m_Transport->TryReceiveSome(data, capacity);
    }

    void StreamClient::Watch(
        IoContext &context,
        IoInterest interest,
        IoContext::Handler handler)
    {
        EnsureStreamTransport(m_Transport);
        if (!m_Transport->IsConnected())
            throw std::logic_error("Stream client is not connected");
        m_Registration.Reset();
        m_Registration = m_Transport->Watch(context, interest, std::move(handler));
        m_ReactorMode = true;
    }

    void StreamClient::SendAll(const void *data, std::size_t size)
    {
        EnsureBlockingEndpointMode(m_ReactorMode, "stream client");
        EnsureStreamTransport(m_Transport);
        if (!m_Transport->IsConnected())
            throw std::logic_error("Stream client is not connected");
        SendAllBytes(*m_Transport, data, size);
    }

    void StreamClient::SendAll(const Buffer &buffer)
    {
        if (buffer.Size > std::numeric_limits<std::size_t>::max())
            throw std::length_error("Stream send buffer is too large");
        SendAll(buffer.Data, static_cast<std::size_t>(buffer.Size));
    }

    std::uint64_t StreamClient::ReceiveSome(Buffer &buffer)
    {
        EnsureBlockingEndpointMode(m_ReactorMode, "stream client");
        EnsureStreamTransport(m_Transport);
        if (!m_Transport->IsConnected())
            throw std::logic_error("Stream client is not connected");
        return ReceiveBuffer(*m_Transport, buffer);
    }

    void StreamClient::Close() noexcept
    {
        m_Registration.Reset();
        if (m_Transport)
            m_Transport->Close();
    }

    bool StreamClient::IsConnected() const noexcept
    {
        return m_Transport && m_Transport->IsConnected();
    }

    std::optional<SocketAddr> StreamClient::GetLocalAddr() const
    {
        return m_Transport ? m_Transport->GetLocalAddr() : std::nullopt;
    }

    std::optional<SocketAddr> StreamClient::GetPeerAddr() const
    {
        return m_Transport ? m_Transport->GetPeerAddr() : std::nullopt;
    }

    StreamServer::StreamServer()
        : m_Transport(CreateDefaultStreamServerTransport())
    {
    }

    StreamServer::StreamServer(std::unique_ptr<StreamServerTransport> transport)
        : m_Transport(std::move(transport))
    {
        if (!m_Transport)
            throw std::invalid_argument("Stream server transport must not be null");
    }

    bool StreamServer::Listen(const SocketAddr &address, int backlog)
    {
        if (!m_Transport)
            throw std::logic_error("Stream server transport is not configured");
        m_Registration.Reset();
        m_ReactorMode = false;
        return m_Transport->Listen(address, backlog);
    }

    bool StreamServer::SetTimeout(
        SocketTimeoutDirection direction,
        std::chrono::milliseconds timeout)
    {
        ValidateTimeout(direction, timeout);
        return m_Transport && m_Transport->SetTimeout(direction, timeout);
    }

    std::optional<StreamConnection> StreamServer::Accept()
    {
        EnsureBlockingEndpointMode(m_ReactorMode, "stream server");
        if (!m_Transport || !m_Transport->IsListening())
            throw std::logic_error("Stream server is not listening");

        auto accepted = m_Transport->Accept();
        if (!accepted)
            return std::nullopt;
        return StreamConnection(std::move(accepted));
    }

    SocketIoResult<StreamConnection> StreamServer::TryAccept()
    {
        if (!m_Transport || !m_Transport->IsListening())
            throw std::logic_error("Stream server is not listening");

        m_ReactorMode = true;
        auto accepted = m_Transport->TryAccept();
        if (accepted.status != SocketIoStatus::Completed)
            return {accepted.status, std::nullopt};
        if (!accepted.value || !*accepted.value)
            throw std::logic_error("Stream server transport accepted without a connection");

        SocketIoResult<StreamConnection> result;
        result.status = SocketIoStatus::Completed;
        result.value.emplace(StreamConnection(std::move(*accepted.value)));
        result.value->m_ReactorMode = true;
        return result;
    }

    void StreamServer::Watch(
        IoContext &context,
        IoInterest interest,
        IoContext::Handler handler)
    {
        if (!m_Transport || !m_Transport->IsListening())
            throw std::logic_error("Stream server is not listening");
        m_Registration.Reset();
        m_Registration = m_Transport->Watch(context, interest, std::move(handler));
        m_ReactorMode = true;
    }

    void StreamServer::Close() noexcept
    {
        m_Registration.Reset();
        if (m_Transport)
            m_Transport->Close();
    }

    bool StreamServer::IsListening() const noexcept
    {
        return m_Transport && m_Transport->IsListening();
    }

    std::optional<SocketAddr> StreamServer::GetLocalAddr() const
    {
        return m_Transport ? m_Transport->GetLocalAddr() : std::nullopt;
    }

    DatagramClient::DatagramClient(std::unique_ptr<DatagramTransport> transport)
        : m_Transport(std::move(transport))
    {
        if (!m_Transport)
            throw std::invalid_argument("Datagram client transport must not be null");
    }

    bool DatagramClient::Bind(const SocketAddr &address)
    {
        EnsureBlockingEndpointMode(m_ReactorMode, "datagram client");
        if (!m_Transport)
            throw std::logic_error("Datagram client transport is not configured");
        return m_Transport->Bind(address);
    }

    bool DatagramClient::SetTimeout(
        SocketTimeoutDirection direction,
        std::chrono::milliseconds timeout)
    {
        ValidateTimeout(direction, timeout);
        return m_Transport && m_Transport->SetTimeout(direction, timeout);
    }

    std::size_t DatagramClient::SendTo(
        const SocketAddr &peer,
        const void *data,
        std::size_t size)
    {
        EnsureBlockingEndpointMode(m_ReactorMode, "datagram client");
        if (!m_Transport)
            throw std::logic_error("Datagram client transport is not configured");
        return m_Transport->SendTo(peer, data, size);
    }

    SocketIoResult<std::size_t> DatagramClient::TrySendTo(
        const SocketAddr &peer,
        const void *data,
        std::size_t size)
    {
        if (!m_Transport || !m_Transport->IsValid())
            throw std::logic_error("Datagram client is closed");
        m_ReactorMode = true;
        return m_Transport->TrySendTo(peer, data, size);
    }

    DatagramReceiveResult DatagramClient::ReceiveFrom(void *data, std::size_t capacity)
    {
        EnsureBlockingEndpointMode(m_ReactorMode, "datagram client");
        if (!m_Transport)
            throw std::logic_error("Datagram client transport is not configured");
        return m_Transport->ReceiveFrom(data, capacity);
    }

    SocketIoResult<DatagramReceiveResult> DatagramClient::TryReceiveFrom(
        void *data,
        std::size_t capacity)
    {
        if (!m_Transport || !m_Transport->IsValid())
            throw std::logic_error("Datagram client is closed");
        m_ReactorMode = true;
        return m_Transport->TryReceiveFrom(data, capacity);
    }

    void DatagramClient::Watch(
        IoContext &context,
        IoInterest interest,
        IoContext::Handler handler)
    {
        if (!m_Transport || !m_Transport->IsValid())
            throw std::logic_error("Datagram client is closed");
        m_Registration.Reset();
        m_Registration = m_Transport->Watch(context, interest, std::move(handler));
        m_ReactorMode = true;
    }

    void DatagramClient::Close() noexcept
    {
        m_Registration.Reset();
        if (m_Transport)
            m_Transport->Close();
    }

    bool DatagramClient::IsValid() const noexcept
    {
        return m_Transport && m_Transport->IsValid();
    }

    std::optional<SocketAddr> DatagramClient::GetLocalAddr() const
    {
        return m_Transport ? m_Transport->GetLocalAddr() : std::nullopt;
    }

    DatagramServer::DatagramServer(std::unique_ptr<DatagramTransport> transport)
        : m_Transport(std::move(transport))
    {
        if (!m_Transport)
            throw std::invalid_argument("Datagram server transport must not be null");
    }

    bool DatagramServer::Bind(const SocketAddr &address)
    {
        EnsureBlockingEndpointMode(m_ReactorMode, "datagram server");
        if (!m_Transport)
            throw std::logic_error("Datagram server transport is not configured");
        return m_Transport->Bind(address);
    }

    bool DatagramServer::SetTimeout(
        SocketTimeoutDirection direction,
        std::chrono::milliseconds timeout)
    {
        ValidateTimeout(direction, timeout);
        return m_Transport && m_Transport->SetTimeout(direction, timeout);
    }

    std::size_t DatagramServer::SendTo(
        const SocketAddr &peer,
        const void *data,
        std::size_t size)
    {
        EnsureBlockingEndpointMode(m_ReactorMode, "datagram server");
        if (!m_Transport)
            throw std::logic_error("Datagram server transport is not configured");
        return m_Transport->SendTo(peer, data, size);
    }

    SocketIoResult<std::size_t> DatagramServer::TrySendTo(
        const SocketAddr &peer,
        const void *data,
        std::size_t size)
    {
        if (!m_Transport || !m_Transport->IsBound())
            throw std::logic_error("Datagram server is not bound");
        m_ReactorMode = true;
        return m_Transport->TrySendTo(peer, data, size);
    }

    DatagramReceiveResult DatagramServer::ReceiveFrom(void *data, std::size_t capacity)
    {
        EnsureBlockingEndpointMode(m_ReactorMode, "datagram server");
        if (!m_Transport)
            throw std::logic_error("Datagram server transport is not configured");
        return m_Transport->ReceiveFrom(data, capacity);
    }

    SocketIoResult<DatagramReceiveResult> DatagramServer::TryReceiveFrom(
        void *data,
        std::size_t capacity)
    {
        if (!m_Transport || !m_Transport->IsBound())
            throw std::logic_error("Datagram server is not bound");
        m_ReactorMode = true;
        return m_Transport->TryReceiveFrom(data, capacity);
    }

    void DatagramServer::Watch(
        IoContext &context,
        IoInterest interest,
        IoContext::Handler handler)
    {
        if (!m_Transport || !m_Transport->IsBound())
            throw std::logic_error("Datagram server is not bound");
        m_Registration.Reset();
        m_Registration = m_Transport->Watch(context, interest, std::move(handler));
        m_ReactorMode = true;
    }

    void DatagramServer::Close() noexcept
    {
        m_Registration.Reset();
        if (m_Transport)
            m_Transport->Close();
    }

    bool DatagramServer::IsBound() const noexcept
    {
        return m_Transport && m_Transport->IsBound();
    }

    std::optional<SocketAddr> DatagramServer::GetLocalAddr() const
    {
        return m_Transport ? m_Transport->GetLocalAddr() : std::nullopt;
    }
}
