#include "../TcpClientImpl.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

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

        class TcpClientWindowsImpl final : public TcpClientImpl
        {
        public:
            void Connect(const std::string &host, std::uint16_t port) override
            {
                if (host.empty())
                    throw std::invalid_argument("TCP host must not be empty");
                if (port == 0)
                    throw std::invalid_argument("TCP port must not be zero");

                Close();
                static WinsockRuntime runtime;
                (void)runtime;

                const std::string service = std::to_string(port);
                addrinfo hints{};
                hints.ai_family = AF_UNSPEC;
                hints.ai_socktype = SOCK_STREAM;
                hints.ai_protocol = IPPROTO_TCP;

                addrinfo *addresses = nullptr;
                const int resolveError =
                    getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses);
                if (resolveError != 0)
                    ThrowSocketError("getaddrinfo", resolveError);

                int lastError = 0;
                for (addrinfo *address = addresses; address != nullptr; address = address->ai_next)
                {
                    const SOCKET socket =
                        ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
                    if (socket == INVALID_SOCKET)
                    {
                        lastError = WSAGetLastError();
                        continue;
                    }

                    if (::connect(
                            socket,
                            address->ai_addr,
                            static_cast<int>(address->ai_addrlen)) == 0)
                    {
                        m_Socket = socket;
                        freeaddrinfo(addresses);
                        return;
                    }

                    lastError = WSAGetLastError();
                    closesocket(socket);
                }

                freeaddrinfo(addresses);
                if (lastError == 0)
                    throw NetworkError("connect", 0, "no usable address was returned");
                ThrowSocketError("connect", lastError);
            }

            void SendAll(const Buffer &buffer) override
            {
                EnsureConnected();
                std::uint64_t sent = 0;
                const auto maxChunk =
                    static_cast<std::uint64_t>(std::numeric_limits<int>::max());

                while (sent < buffer.Size)
                {
                    const int chunkSize = static_cast<int>(
                        std::min(buffer.Size - sent, maxChunk));
                    const int result = ::send(
                        m_Socket,
                        reinterpret_cast<const char *>(buffer.Data + sent),
                        chunkSize,
                        0);
                    if (result > 0)
                    {
                        sent += static_cast<std::uint64_t>(result);
                        continue;
                    }
                    if (result == SOCKET_ERROR)
                    {
                        const int error = WSAGetLastError();
                        if (error == WSAEINTR)
                            continue;
                        ThrowSocketError("send", error);
                    }
                    throw NetworkError("send", 0, "socket accepted no data");
                }
            }

            std::uint64_t ReceiveSome(Buffer &buffer) override
            {
                EnsureConnected();
                if (buffer.Capacity == 0)
                {
                    buffer.Size = 0;
                    return 0;
                }
                if (buffer.Data == nullptr)
                    throw std::invalid_argument("TCP receive buffer has no allocated storage");

                const int chunkSize = static_cast<int>(std::min(
                    buffer.Capacity,
                    static_cast<std::uint64_t>(std::numeric_limits<int>::max())));
                for (;;)
                {
                    const int result = ::recv(
                        m_Socket,
                        reinterpret_cast<char *>(buffer.Data),
                        chunkSize,
                        0);
                    if (result >= 0)
                    {
                        buffer.Size = static_cast<std::uint64_t>(result);
                        return buffer.Size;
                    }

                    const int error = WSAGetLastError();
                    if (error == WSAEINTR)
                        continue;
                    ThrowSocketError("recv", error);
                }
            }

            void Close() noexcept override
            {
                if (m_Socket == INVALID_SOCKET)
                    return;
                shutdown(m_Socket, SD_BOTH);
                closesocket(m_Socket);
                m_Socket = INVALID_SOCKET;
            }

            bool IsConnected() const noexcept override
            {
                return m_Socket != INVALID_SOCKET;
            }

        private:
            void EnsureConnected() const
            {
                if (!IsConnected())
                    throw std::logic_error("TCP client is not connected");
            }

            SOCKET m_Socket = INVALID_SOCKET;
        };
    }

    TcpClientImpl *CreateTcpClientImpl()
    {
        return new TcpClientWindowsImpl();
    }
}
