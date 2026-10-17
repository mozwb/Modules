#include "Network/TcpClient.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using NativeSocket = SOCKET;
constexpr NativeSocket InvalidSocket = INVALID_SOCKET;
void CloseNativeSocket(NativeSocket socket) { closesocket(socket); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using NativeSocket = int;
constexpr NativeSocket InvalidSocket = -1;
void CloseNativeSocket(NativeSocket socket) { close(socket); }
#endif

namespace
{
#ifdef _WIN32
    class WinsockSession
    {
    public:
        WinsockSession()
        {
            WSADATA data{};
            m_Started = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        }

        ~WinsockSession()
        {
            if (m_Started)
                WSACleanup();
        }

        bool Started() const { return m_Started; }

    private:
        bool m_Started = false;
    };
#endif

    bool RunSelfTest()
    {
#ifdef _WIN32
        WinsockSession winsock;
        if (!winsock.Started())
            return false;
#endif

        NativeSocket listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == InvalidSocket)
            return false;

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
            listen(listener, 1) != 0)
        {
            CloseNativeSocket(listener);
            return false;
        }

#ifdef _WIN32
        int addressLength = sizeof(address);
#else
        socklen_t addressLength = sizeof(address);
#endif
        if (getsockname(listener, reinterpret_cast<sockaddr *>(&address), &addressLength) != 0)
        {
            CloseNativeSocket(listener);
            return false;
        }
        const std::uint16_t port = ntohs(address.sin_port);
        const std::string message = "Network TCP self-test";

        std::thread server;
        bool passed = false;
        try
        {
            X_Y::TcpClient client;
            client.Connect("127.0.0.1", port);

            server = std::thread([listener, messageSize = message.size()]
            {
                NativeSocket peer = accept(listener, nullptr, nullptr);
                if (peer != InvalidSocket)
                {
                    std::array<char, 256> buffer{};
                    std::size_t receivedTotal = 0;
                    while (receivedTotal < messageSize)
                    {
#ifdef _WIN32
                        const int received = recv(
                            peer,
                            buffer.data() + receivedTotal,
                            static_cast<int>(messageSize - receivedTotal),
                            0);
#else
                        const ssize_t received = recv(
                            peer,
                            buffer.data() + receivedTotal,
                            messageSize - receivedTotal,
                            0);
#endif
                        if (received <= 0)
                            break;
                        receivedTotal += static_cast<std::size_t>(received);
                    }

                    std::size_t sentTotal = 0;
                    while (sentTotal < receivedTotal)
                    {
#ifdef _WIN32
                        const int sent = send(
                            peer,
                            buffer.data() + sentTotal,
                            static_cast<int>(receivedTotal - sentTotal),
                            0);
#else
                        const ssize_t sent = send(
                            peer,
                            buffer.data() + sentTotal,
                            receivedTotal - sentTotal,
                            0);
#endif
                        if (sent <= 0)
                            break;
                        sentTotal += static_cast<std::size_t>(sent);
                    }
                    CloseNativeSocket(peer);
                }
                CloseNativeSocket(listener);
            });

            X_Y::Buffer outgoing;
            outgoing.Overwrite(message.data(), message.size());
            client.SendAll(outgoing);

            X_Y::Buffer chunk(256);
            X_Y::Buffer response;
            std::uint64_t received = 0;
            while (received < message.size())
            {
                const std::uint64_t count = client.ReceiveSome(chunk);
                if (count == 0)
                    break;
                response.Append(chunk.Data, chunk.Size);
                received += count;
            }
            passed = received == message.size() &&
                     std::memcmp(response.Data, message.data(), message.size()) == 0;
        }
        catch (const std::exception &error)
        {
            std::cerr << "Network self-test failed: " << error.what() << '\n';
        }

        if (server.joinable())
            server.join();
        else
            CloseNativeSocket(listener);
        return passed;
    }
}

int main()
{
    if (!RunSelfTest())
    {
        std::cerr << "TCP loopback echo test did not pass\n";
        return 1;
    }
    std::cout << "TCP loopback echo test passed\n";
    return 0;
}
