#include "Network/SocketHandle.h"
#include "Network/Endpoints.h"
#include "Network/Tcp.h"

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

    bool RunSocketHandleSelfTest()
    {
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

        const auto destination = X_Y::SocketAddr::Parse(
            "127.0.0.1:" + std::to_string(ntohs(address.sin_port)));
        if (!destination)
        {
            CloseNativeSocket(listener);
            return false;
        }

        X_Y::SocketHandle client;
        try
        {
            client.Connect(*destination);
        }
        catch (const std::exception &error)
        {
            std::cerr << "SocketHandle connect test failed: " << error.what() << '\n';
            CloseNativeSocket(listener);
            return false;
        }

        const std::string message = "SocketHandle send/receive self-test";
        bool serverPassed = false;
        std::thread server([&]
        {
            NativeSocket peer = accept(listener, nullptr, nullptr);
            if (peer != InvalidSocket)
            {
                std::array<char, 256> buffer{};
                std::size_t receivedTotal = 0;
                while (receivedTotal < message.size())
                {
#ifdef _WIN32
                    const int received = recv(
                        peer,
                        buffer.data() + receivedTotal,
                        static_cast<int>(message.size() - receivedTotal),
                        0);
#else
                    const ssize_t received = recv(
                        peer,
                        buffer.data() + receivedTotal,
                        message.size() - receivedTotal,
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
                serverPassed = receivedTotal == message.size() && sentTotal == receivedTotal;
                CloseNativeSocket(peer);
            }
            CloseNativeSocket(listener);
        });

        bool clientPassed = false;
        try
        {
            const auto peer = client.GetPeerAddr();
            if (!peer || peer->family != X_Y::AddressFamily::IPv4 ||
                peer->port != destination->port)
                throw std::runtime_error("SocketHandle returned an unexpected peer address");

            std::size_t sent = 0;
            while (sent < message.size())
            {
                const std::size_t count =
                    client.SendSome(message.data() + sent, message.size() - sent);
                if (count == 0)
                    break;
                sent += count;
            }

            std::array<char, 256> response{};
            std::size_t received = 0;
            while (received < message.size())
            {
                const std::size_t count = client.ReceiveSome(
                    response.data() + received,
                    response.size() - received);
                if (count == 0)
                    break;
                received += count;
            }
            clientPassed = sent == message.size() &&
                           received == message.size() &&
                           std::memcmp(response.data(), message.data(), message.size()) == 0;
        }
        catch (const std::exception &error)
        {
            std::cerr << "SocketHandle data-transfer test failed: " << error.what() << '\n';
        }
        client.Close();
        server.join();
        return clientPassed && serverPassed;
    }

    bool RunStreamServerSelfTest()
    {
        X_Y::StreamServer server;
        const auto anyPort = X_Y::SocketAddr::Parse("127.0.0.1:0");
        if (!anyPort || !server.Listen(*anyPort))
            return false;

        const auto listenAddress = server.GetLocalAddr();
        if (!listenAddress || listenAddress->port == 0)
            return false;

        X_Y::StreamClient client;
        try
        {
            client.Connect("127.0.0.1", listenAddress->port);
        }
        catch (const std::exception &error)
        {
            std::cerr << "Stream endpoint connect test failed: " << error.what() << '\n';
            return false;
        }

        const std::string message = "stream endpoint echo self-test";
        bool serverPassed = false;
        std::thread serverThread([&]
        {
            try
            {
                auto connection = server.Accept();
                if (!connection)
                    return;

                std::array<char, 128> buffer{};
                std::size_t received = 0;
                while (received < message.size())
                {
                    const std::size_t count = connection->ReceiveSome(
                        buffer.data() + received,
                        buffer.size() - received);
                    if (count == 0)
                        return;
                    received += count;
                }
                connection->SendAll(buffer.data(), received);
                serverPassed = true;
            }
            catch (const std::exception &error)
            {
                std::cerr << "Stream endpoint server test failed: " << error.what() << '\n';
            }
        });

        bool clientPassed = false;
        try
        {
            client.SendAll(message.data(), message.size());
            std::array<char, 128> response{};
            std::size_t received = 0;
            while (received < message.size())
            {
                const std::size_t count =
                    client.ReceiveSome(response.data() + received, response.size() - received);
                if (count == 0)
                    break;
                received += count;
            }
            clientPassed = received == message.size() &&
                           std::memcmp(response.data(), message.data(), message.size()) == 0;
        }
        catch (const std::exception &error)
        {
            std::cerr << "Stream endpoint client test failed: " << error.what() << '\n';
        }
        client.Close();
        serverThread.join();
        server.Close();
        return clientPassed && serverPassed;
    }

    bool RunDatagramSelfTest()
    {
        const auto anyPort = X_Y::SocketAddr::Parse("127.0.0.1:0");
        if (!anyPort)
            return false;

        X_Y::DatagramServer server;
        if (server.IsBound() || !server.Bind(*anyPort) || !server.IsBound())
            return false;
        const auto serverAddress = server.GetLocalAddr();
        if (!serverAddress || serverAddress->port == 0)
            return false;

        const std::string message = "datagram truncation self-test";
        X_Y::DatagramClient client;
        try
        {
            if (client.SendTo(*serverAddress, message.data(), message.size()) != message.size())
                return false;
        }
        catch (const std::exception &error)
        {
            std::cerr << "Datagram send test failed: " << error.what() << '\n';
            return false;
        }

        bool serverPassed = false;
        std::thread serverThread([&]
        {
            try
            {
                std::array<char, 8> buffer{};
                const auto packet = server.ReceiveFrom(buffer.data(), buffer.size());
                serverPassed = packet.truncated &&
                               packet.size == buffer.size() &&
                               packet.peer.family == X_Y::AddressFamily::IPv4;
                if (serverPassed)
                    server.SendTo(packet.peer, buffer.data(), packet.size);
            }
            catch (const std::exception &error)
            {
                std::cerr << "Datagram server test failed: " << error.what() << '\n';
            }
        });

        bool clientPassed = false;
        try
        {
            std::array<char, 64> response{};
            const auto packet = client.ReceiveFrom(response.data(), response.size());
            clientPassed = !packet.truncated &&
                           packet.size == 8 &&
                           packet.peer.port == serverAddress->port &&
                           std::memcmp(response.data(), message.data(), packet.size) == 0;
        }
        catch (const std::exception &error)
        {
            std::cerr << "Datagram client test failed: " << error.what() << '\n';
        }
        serverThread.join();
        server.Close();
        return clientPassed && serverPassed;
    }

    bool RunSelfTest()
    {
#ifdef _WIN32
        WinsockSession winsock;
        if (!winsock.Started())
            return false;
#endif

        if (!RunSocketHandleSelfTest())
            return false;
        if (!RunStreamServerSelfTest() || !RunDatagramSelfTest())
            return false;

        const auto ipv4 = X_Y::SocketAddr::Parse("127.0.0.1:0");
        const auto ipv6 = X_Y::SocketAddr::Parse("[::1]:65535");
        const auto scopedIpv6 = X_Y::SocketAddr::Parse("[fe80::1%2]:80");
        if (!ipv4 || ipv4->ToString() != "127.0.0.1:0" ||
            !ipv6 || ipv6->ToString() != "[::1]:65535" ||
            !scopedIpv6 || scopedIpv6->ToString() != "[fe80::1%2]:80" ||
            X_Y::SocketAddr::Parse("127.0.0.1:65536") ||
            X_Y::SocketAddr::Parse("[::1]") ||
            X_Y::SocketAddr::Parse("[fe80::1%zone]:80"))
            return false;

        X_Y::SocketHandle socket;
        const int reuseAddress = 1;
        if (!socket.IsValid() ||
            !socket.SetOption(
                X_Y::SocketOptionId::ReuseAddr,
                &reuseAddress,
                sizeof(reuseAddress)) ||
            !socket.SetNonBlocking(true) ||
            !socket.SetNonBlocking(false) ||
            !socket.Bind(*ipv4))
            return false;
        const auto boundAddress = socket.GetLocalAddr();
        if (!boundAddress || boundAddress->port == 0 ||
            boundAddress->family != X_Y::AddressFamily::IPv4)
            return false;
        socket.Close();
        if (socket.IsValid())
            return false;

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
