#include "../TcpClientImpl.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <netdb.h>
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

        class TcpClientLinuxImpl final : public TcpClientImpl
        {
        public:
            void Connect(const std::string &host, std::uint16_t port) override
            {
                if (host.empty())
                    throw std::invalid_argument("TCP host must not be empty");
                if (port == 0)
                    throw std::invalid_argument("TCP port must not be zero");

                Close();
                const std::string service = std::to_string(port);
                addrinfo hints{};
                hints.ai_family = AF_UNSPEC;
                hints.ai_socktype = SOCK_STREAM;
                hints.ai_protocol = IPPROTO_TCP;

                addrinfo *addresses = nullptr;
                const int resolveError =
                    getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses);
                if (resolveError != 0)
                    throw NetworkError("getaddrinfo", resolveError, gai_strerror(resolveError));

                int lastError = 0;
                for (addrinfo *address = addresses; address != nullptr; address = address->ai_next)
                {
                    const int socket =
                        ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
                    if (socket < 0)
                    {
                        lastError = errno;
                        continue;
                    }

                    if (::connect(socket, address->ai_addr, address->ai_addrlen) == 0)
                    {
                        m_Socket = socket;
                        freeaddrinfo(addresses);
                        return;
                    }

                    lastError = errno;
                    close(socket);
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

                while (sent < buffer.Size)
                {
                    const std::size_t chunkSize = static_cast<std::size_t>(
                        std::min<std::uint64_t>(
                            buffer.Size - sent,
                            static_cast<std::uint64_t>(std::numeric_limits<ssize_t>::max())));
                    const ssize_t result = ::send(
                        m_Socket,
                        buffer.Data + sent,
                        chunkSize,
                        MSG_NOSIGNAL);
                    if (result > 0)
                    {
                        sent += static_cast<std::uint64_t>(result);
                        continue;
                    }
                    if (result < 0)
                    {
                        const int error = errno;
                        if (error == EINTR)
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

                const std::size_t chunkSize = static_cast<std::size_t>(
                    std::min<std::uint64_t>(
                        buffer.Capacity,
                        static_cast<std::uint64_t>(std::numeric_limits<ssize_t>::max())));
                for (;;)
                {
                    const ssize_t result = ::recv(m_Socket, buffer.Data, chunkSize, 0);
                    if (result >= 0)
                    {
                        buffer.Size = static_cast<std::uint64_t>(result);
                        return buffer.Size;
                    }

                    const int error = errno;
                    if (error == EINTR)
                        continue;
                    ThrowSocketError("recv", error);
                }
            }

            void Close() noexcept override
            {
                if (m_Socket < 0)
                    return;
                shutdown(m_Socket, SHUT_RDWR);
                close(m_Socket);
                m_Socket = -1;
            }

            bool IsConnected() const noexcept override
            {
                return m_Socket >= 0;
            }

        private:
            void EnsureConnected() const
            {
                if (!IsConnected())
                    throw std::logic_error("TCP client is not connected");
            }

            int m_Socket = -1;
        };
    }

    TcpClientImpl *CreateTcpClientImpl()
    {
        return new TcpClientLinuxImpl();
    }
}
