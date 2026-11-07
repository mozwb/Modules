#include "Network/Endpoints.h"
#include "Network/IoContext.h"

#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
    void Require(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    X_Y::SocketAddr ParseAddress(const char *text)
    {
        const auto address = X_Y::SocketAddr::Parse(text);
        if (!address)
            throw std::runtime_error(std::string("invalid test address: ") + text);
        return *address;
    }

    void TestAddressParsing()
    {
        const auto ipv4 = ParseAddress("127.0.0.1:8080");
        Require(ipv4.family == X_Y::AddressFamily::IPv4, "IPv4 family was not parsed");
        Require(ipv4.port == 8080, "IPv4 port was not parsed");
        Require(ipv4.ToString() == "127.0.0.1:8080", "IPv4 address did not round-trip");

        const auto ipv6 = ParseAddress("[::1]:8080");
        Require(ipv6.family == X_Y::AddressFamily::IPv6, "IPv6 family was not parsed");
        Require(ipv6.port == 8080, "IPv6 port was not parsed");
        Require(ipv6.ToString() == "[::1]:8080", "IPv6 address did not round-trip");
        Require(!X_Y::SocketAddr::Parse("127.0.0.1"), "address without a port was accepted");
        Require(!X_Y::SocketAddr::Parse("[::1]8080"), "malformed IPv6 endpoint was accepted");
    }

    void TestTcpLoopback()
    {
        X_Y::StreamServer server;
        Require(server.Listen(ParseAddress("127.0.0.1:0")), "TCP listen failed");

        const auto localAddress = server.GetLocalAddr();
        Require(localAddress.has_value(), "TCP server did not report its local address");
        Require(localAddress->port != 0, "TCP server did not bind an ephemeral port");

        X_Y::StreamClient client;
        client.Connect("127.0.0.1", localAddress->port);
        client.SendAll("ping", 4);

        std::exception_ptr serverError;
        std::string received;
        bool observedPeerClose = false;
        std::thread serverThread([&]
        {
            try
            {
                auto connection = server.Accept();
                Require(connection.has_value(), "TCP accept returned no connection");

                std::array<char, 16> buffer{};
                while (received.size() < 4)
                {
                    const auto count = connection->ReceiveSome(buffer.data(), buffer.size());
                    Require(count != 0, "TCP peer closed before sending the full message");
                    received.append(buffer.data(), count);
                }
                connection->SendAll(received.data(), received.size());
                observedPeerClose = connection->ReceiveSome(buffer.data(), buffer.size()) == 0;
            }
            catch (...)
            {
                serverError = std::current_exception();
            }
        });

        std::string echoed;
        std::array<char, 16> response{};
        std::exception_ptr clientError;
        try
        {
            while (echoed.size() < 4)
            {
                const auto count = client.ReceiveSome(response.data(), response.size());
                Require(count != 0, "TCP server closed before echoing the full message");
                echoed.append(response.data(), count);
            }
        }
        catch (...)
        {
            clientError = std::current_exception();
        }

        client.Close();
        serverThread.join();

        if (clientError)
            std::rethrow_exception(clientError);
        if (serverError)
            std::rethrow_exception(serverError);
        Require(received == "ping", "TCP server received unexpected bytes");
        Require(echoed == "ping", "TCP client received unexpected bytes");
        Require(observedPeerClose, "TCP close was not reported as end-of-stream");
    }

    void TestUdpLoopback()
    {
        X_Y::DatagramServer server;
        Require(server.Bind(ParseAddress("127.0.0.1:0")), "UDP bind failed");

        const auto localAddress = server.GetLocalAddr();
        Require(localAddress.has_value(), "UDP server did not report its local address");
        Require(localAddress->port != 0, "UDP server did not bind an ephemeral port");

        X_Y::DatagramClient client;
        const std::string message = "datagram";
        Require(
            client.SendTo(*localAddress, message.data(), message.size()) == message.size(),
            "UDP client sent an incomplete datagram");

        std::array<char, 32> buffer{};
        const auto request = server.ReceiveFrom(buffer.data(), buffer.size());
        Require(!request.truncated, "UDP request was truncated");
        Require(
            std::string(buffer.data(), request.size) == message,
            "UDP server received unexpected bytes");

        const std::string reply = "ack";
        Require(
            server.SendTo(request.peer, reply.data(), reply.size()) == reply.size(),
            "UDP server sent an incomplete datagram");
        const auto response = client.ReceiveFrom(buffer.data(), buffer.size());
        Require(!response.truncated, "UDP response was truncated");
        Require(
            std::string(buffer.data(), response.size) == reply,
            "UDP client received unexpected bytes");
    }

    void TestReactorUdpReadWriteAndStop()
    {
        X_Y::SocketHandle receiver(
            X_Y::AddressFamily::IPv4,
            X_Y::SocketType::Datagram);
        Require(
            receiver.Bind(ParseAddress("127.0.0.1:0")),
            "Reactor UDP receiver bind failed");
        const auto receiverAddress = receiver.GetLocalAddr();
        Require(receiverAddress.has_value(), "Reactor UDP receiver has no local address");

        X_Y::SocketHandle sender(
            X_Y::AddressFamily::IPv4,
            X_Y::SocketType::Datagram);

        X_Y::IoContext io;
        bool receivedData = false;
        bool observedWritable = false;
        std::exception_ptr handlerError;
        X_Y::IoContext::Registration registration;
        registration = io.Watch(
            receiver,
            X_Y::IoInterest::Read,
            [&](X_Y::IoReady ready)
            {
                try
                {
                    if (X_Y::HasIoReady(ready, X_Y::IoReady::Error) ||
                        X_Y::HasIoReady(ready, X_Y::IoReady::Closed))
                        throw std::runtime_error("Reactor reported a UDP socket failure");

                    if (X_Y::HasIoReady(ready, X_Y::IoReady::Read))
                    {
                        std::array<char, 16> buffer{};
                        const auto result =
                            receiver.ReceiveFrom(buffer.data(), buffer.size());
                        Require(result.size == 4, "Reactor UDP datagram size mismatch");
                        Require(std::string(buffer.data(), result.size) == "ping",
                                "Reactor UDP datagram payload mismatch");
                        receivedData = true;
                        Require(
                            registration.Update(X_Y::IoInterest::Write),
                            "Reactor failed to update socket interest");
                        return;
                    }

                    if (X_Y::HasIoReady(ready, X_Y::IoReady::Write))
                    {
                        observedWritable = true;
                        io.Stop();
                    }
                }
                catch (...)
                {
                    handlerError = std::current_exception();
                    io.Stop();
                }
            });

        std::exception_ptr runError;
        std::thread eventThread([&]
        {
            try
            {
                io.Run();
            }
            catch (...)
            {
                runError = std::current_exception();
            }
        });

        const std::string message = "ping";
        Require(
            sender.SendTo(*receiverAddress, message.data(), message.size()) == message.size(),
            "Reactor UDP test send failed");
        eventThread.join();

        if (runError)
            std::rethrow_exception(runError);
        if (handlerError)
            std::rethrow_exception(handlerError);
        Require(receivedData, "Reactor did not report UDP readability");
        Require(observedWritable, "Reactor did not report UDP writability after update");
        registration.Reset();
        Require(!registration, "Reactor registration remained active after reset");
        registration.Reset();
        Require(!registration, "Reactor registration reset was not idempotent");
    }

    void TestReactorStopFromAnotherThread()
    {
        X_Y::IoContext io;
        std::atomic<bool> runEntered{false};
        std::exception_ptr runError;
        std::thread eventThread([&]
        {
            runEntered.store(true, std::memory_order_release);
            try
            {
                io.Run();
            }
            catch (...)
            {
                runError = std::current_exception();
            }
        });

        while (!runEntered.load(std::memory_order_acquire))
            std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        io.Stop();
        eventThread.join();
        if (runError)
            std::rethrow_exception(runError);
        Require(io.IsStopped(), "IoContext did not preserve its stopped state");
    }

    void TestNonBlockingSocketOperations()
    {
        X_Y::SocketHandle udpReceiver(
            X_Y::AddressFamily::IPv4,
            X_Y::SocketType::Datagram);
        Require(
            udpReceiver.Bind(ParseAddress("127.0.0.1:0")),
            "non-blocking UDP bind failed");
        Require(udpReceiver.SetNonBlocking(true), "UDP non-blocking mode failed");
        std::array<char, 16> datagramBuffer{};
        const auto emptyReceive =
            udpReceiver.TryReceiveFrom(datagramBuffer.data(), datagramBuffer.size());
        Require(
            emptyReceive.status == X_Y::SocketIoStatus::WouldBlock &&
                !emptyReceive.value,
            "empty UDP receive did not report WouldBlock");

        X_Y::SocketHandle udpSender(
            X_Y::AddressFamily::IPv4,
            X_Y::SocketType::Datagram);
        Require(udpSender.SetNonBlocking(true), "UDP sender non-blocking mode failed");
        const auto udpAddress = udpReceiver.GetLocalAddr();
        Require(udpAddress.has_value(), "non-blocking UDP receiver has no address");
        const std::string datagram = "udp";
        const auto sentDatagram =
            udpSender.TrySendTo(*udpAddress, datagram.data(), datagram.size());
        Require(
            sentDatagram.status == X_Y::SocketIoStatus::Completed &&
                sentDatagram.value == datagram.size(),
            "non-blocking UDP send failed");
        const auto receivedDatagram =
            udpReceiver.TryReceiveFrom(datagramBuffer.data(), datagramBuffer.size());
        Require(
            receivedDatagram.status == X_Y::SocketIoStatus::Completed &&
                receivedDatagram.value &&
                receivedDatagram.value->size == datagram.size() &&
                std::string(datagramBuffer.data(), receivedDatagram.value->size) == datagram,
            "non-blocking UDP receive failed");

        X_Y::SocketHandle listener(
            X_Y::AddressFamily::IPv4,
            X_Y::SocketType::Stream);
        Require(listener.Bind(ParseAddress("127.0.0.1:0")), "non-blocking TCP bind failed");
        Require(listener.Listen(), "non-blocking TCP listen failed");
        Require(listener.SetNonBlocking(true), "TCP listener non-blocking mode failed");
        const auto emptyAccept = listener.TryAccept();
        Require(
            emptyAccept.status == X_Y::SocketIoStatus::WouldBlock &&
                !emptyAccept.value,
            "empty TCP accept did not report WouldBlock");

        const auto tcpAddress = listener.GetLocalAddr();
        Require(tcpAddress.has_value(), "non-blocking TCP listener has no address");
        X_Y::SocketHandle tcpClient(
            X_Y::AddressFamily::IPv4,
            X_Y::SocketType::Stream);
        tcpClient.Connect(*tcpAddress);
        auto accepted = listener.TryAccept();
        Require(
            accepted.status == X_Y::SocketIoStatus::Completed &&
                accepted.value,
            "non-blocking TCP accept failed");

        std::array<char, 16> streamBuffer{};
        const auto emptyStreamReceive =
            accepted.value->TryReceiveSome(streamBuffer.data(), streamBuffer.size());
        Require(
            emptyStreamReceive.status == X_Y::SocketIoStatus::WouldBlock &&
                !emptyStreamReceive.value,
            "empty TCP receive did not report WouldBlock");

        const std::string request = "request";
        Require(
            tcpClient.SendSome(request.data(), request.size()) == request.size(),
            "TCP request send failed");
        const auto receivedRequest =
            accepted.value->TryReceiveSome(streamBuffer.data(), streamBuffer.size());
        Require(
            receivedRequest.status == X_Y::SocketIoStatus::Completed &&
                receivedRequest.value == request.size() &&
                std::string(streamBuffer.data(), *receivedRequest.value) == request,
            "non-blocking TCP receive failed");

        const std::string reply = "ok";
        const auto sentReply = accepted.value->TrySendSome(reply.data(), reply.size());
        Require(
            sentReply.status == X_Y::SocketIoStatus::Completed &&
                sentReply.value == reply.size(),
            "non-blocking TCP send failed");
        Require(
            tcpClient.ReceiveSome(streamBuffer.data(), streamBuffer.size()) == reply.size() &&
                std::string(streamBuffer.data(), reply.size()) == reply,
            "TCP reply receive failed");

        tcpClient.Close();
        const auto closedReceive =
            accepted.value->TryReceiveSome(streamBuffer.data(), streamBuffer.size());
        Require(
            closedReceive.status == X_Y::SocketIoStatus::Closed &&
                !closedReceive.value,
            "TCP peer shutdown did not report Closed");
    }

    void TestEndpointReactorAndTryOperations()
    {
        X_Y::DatagramServer udpServer;
        Require(
            udpServer.Bind(ParseAddress("127.0.0.1:0")),
            "Reactor endpoint UDP bind failed");
        const auto udpAddress = udpServer.GetLocalAddr();
        Require(udpAddress.has_value(), "Reactor endpoint UDP server has no address");

        X_Y::IoContext io;
        bool receivedDatagram = false;
        std::exception_ptr handlerError;
        udpServer.Watch(
            io,
            X_Y::IoInterest::Read,
            [&](X_Y::IoReady ready)
            {
                try
                {
                    if (X_Y::HasIoReady(ready, X_Y::IoReady::Error) ||
                        X_Y::HasIoReady(ready, X_Y::IoReady::Closed))
                        throw std::runtime_error("UDP endpoint Reactor reported socket failure");
                    if (!X_Y::HasIoReady(ready, X_Y::IoReady::Read))
                        return;

                    std::array<char, 16> buffer{};
                    const auto result =
                        udpServer.TryReceiveFrom(buffer.data(), buffer.size());
                    Require(
                        result.status == X_Y::SocketIoStatus::Completed &&
                            result.value &&
                            std::string(buffer.data(), result.value->size) == "watch",
                        "UDP endpoint Reactor receive failed");
                    receivedDatagram = true;
                    io.Stop();
                }
                catch (...)
                {
                    handlerError = std::current_exception();
                    io.Stop();
                }
            });
        std::array<char, 8> blockingBuffer{};
        bool rejectedBlockingUdpReceive = false;
        try
        {
            (void)udpServer.ReceiveFrom(blockingBuffer.data(), blockingBuffer.size());
        }
        catch (const std::logic_error &)
        {
            rejectedBlockingUdpReceive = true;
        }
        Require(
            rejectedBlockingUdpReceive,
            "DatagramServer allowed blocking receive after Reactor registration");

        std::exception_ptr runError;
        std::thread eventThread([&]
        {
            try
            {
                io.Run();
            }
            catch (...)
            {
                runError = std::current_exception();
            }
        });

        X_Y::DatagramClient udpClient;
        const std::string datagram = "watch";
        Require(
            udpClient.SendTo(*udpAddress, datagram.data(), datagram.size()) ==
                datagram.size(),
            "Reactor endpoint UDP send failed");
        eventThread.join();
        if (runError)
            std::rethrow_exception(runError);
        if (handlerError)
            std::rethrow_exception(handlerError);
        Require(receivedDatagram, "DatagramServer Reactor callback was not invoked");

        X_Y::StreamServer tcpServer;
        Require(
            tcpServer.Listen(ParseAddress("127.0.0.1:0")),
            "non-blocking Endpoint TCP listen failed");
        const auto tcpAddress = tcpServer.GetLocalAddr();
        Require(tcpAddress.has_value(), "non-blocking Endpoint TCP server has no address");
        const auto noConnection = tcpServer.TryAccept();
        Require(
            noConnection.status == X_Y::SocketIoStatus::WouldBlock &&
                !noConnection.value,
            "StreamServer::TryAccept did not report WouldBlock");
        bool rejectedBlockingAccept = false;
        try
        {
            (void)tcpServer.Accept();
        }
        catch (const std::logic_error &)
        {
            rejectedBlockingAccept = true;
        }
        Require(
            rejectedBlockingAccept,
            "StreamServer allowed blocking Accept after non-blocking mode was enabled");

        X_Y::StreamClient tcpClient;
        tcpClient.Connect("127.0.0.1", tcpAddress->port);
        auto accepted = tcpServer.TryAccept();
        const auto acceptDeadline = std::chrono::steady_clock::now() +
                                    std::chrono::seconds(2);
        while (accepted.status == X_Y::SocketIoStatus::WouldBlock &&
               std::chrono::steady_clock::now() < acceptDeadline)
        {
            std::this_thread::yield();
            accepted = tcpServer.TryAccept();
        }
        Require(
            accepted.status == X_Y::SocketIoStatus::Completed &&
                accepted.value &&
                accepted.value->IsConnected(),
            "StreamServer::TryAccept did not accept the incoming connection");

        std::array<char, 16> buffer{};
        const auto noData = accepted.value->TryReceiveSome(buffer.data(), buffer.size());
        Require(
            noData.status == X_Y::SocketIoStatus::WouldBlock && !noData.value,
            "StreamConnection::TryReceiveSome did not report WouldBlock");
        bool rejectedBlockingReceive = false;
        try
        {
            (void)accepted.value->ReceiveSome(buffer.data(), buffer.size());
        }
        catch (const std::logic_error &)
        {
            rejectedBlockingReceive = true;
        }
        Require(
            rejectedBlockingReceive,
            "StreamConnection allowed blocking receive after non-blocking mode was enabled");

        const std::string request = "try";
        const auto sent = tcpClient.TrySendSome(request.data(), request.size());
        Require(
            sent.status == X_Y::SocketIoStatus::Completed &&
                sent.value == request.size(),
            "StreamClient::TrySendSome failed");
        auto received = accepted.value->TryReceiveSome(buffer.data(), buffer.size());
        const auto receiveDeadline = std::chrono::steady_clock::now() +
                                     std::chrono::seconds(2);
        while (received.status == X_Y::SocketIoStatus::WouldBlock &&
               std::chrono::steady_clock::now() < receiveDeadline)
        {
            std::this_thread::yield();
            received = accepted.value->TryReceiveSome(buffer.data(), buffer.size());
        }
        Require(
            received.status == X_Y::SocketIoStatus::Completed &&
                received.value == request.size() &&
                std::string(buffer.data(), *received.value) == request,
            "StreamConnection::TryReceiveSome failed");
    }

    void TestTcpEndpointReactorEcho()
    {
        X_Y::StreamServer server;
        Require(
            server.Listen(ParseAddress("127.0.0.1:0")),
            "TCP Reactor echo listen failed");
        const auto address = server.GetLocalAddr();
        Require(address.has_value(), "TCP Reactor echo server has no address");

        X_Y::IoContext io;
        X_Y::StreamClient client;
        client.Connect("127.0.0.1", address->port);

        std::optional<X_Y::StreamConnection> accepted;
        std::exception_ptr handlerError;
        bool receivedRequest = false;
        bool receivedReply = false;
        const std::string request = "reactor request";
        const std::string reply = "reactor reply";

        server.Watch(
            io,
            X_Y::IoInterest::Read,
            [&](X_Y::IoReady ready)
            {
                try
                {
                    if (X_Y::HasIoReady(ready, X_Y::IoReady::Error) ||
                        X_Y::HasIoReady(ready, X_Y::IoReady::Closed))
                        throw std::runtime_error("TCP listener Reactor reported failure");
                    if (!X_Y::HasIoReady(ready, X_Y::IoReady::Read))
                        return;

                    auto result = server.TryAccept();
                    if (result.status == X_Y::SocketIoStatus::WouldBlock)
                        return;
                    Require(
                        result.status == X_Y::SocketIoStatus::Completed &&
                            result.value,
                        "TCP listener Reactor failed to accept connection");
                    accepted.emplace(std::move(*result.value));
                    accepted->Watch(
                        io,
                        X_Y::IoInterest::Read,
                        [&](X_Y::IoReady connectionReady)
                        {
                            try
                            {
                                if (X_Y::HasIoReady(connectionReady, X_Y::IoReady::Error) ||
                                    X_Y::HasIoReady(connectionReady, X_Y::IoReady::Closed))
                                    throw std::runtime_error(
                                        "TCP accepted connection Reactor reported failure");
                                if (!X_Y::HasIoReady(connectionReady, X_Y::IoReady::Read))
                                    return;

                                std::array<char, 64> buffer{};
                                const auto received =
                                    accepted->TryReceiveSome(buffer.data(), buffer.size());
                                if (received.status == X_Y::SocketIoStatus::WouldBlock)
                                    return;
                                Require(
                                    received.status == X_Y::SocketIoStatus::Completed &&
                                        received.value &&
                                        std::string(buffer.data(), *received.value) == request,
                                    "TCP Reactor server received unexpected request");
                                receivedRequest = true;

                                const auto sent = accepted->TrySendSome(
                                    reply.data(),
                                    reply.size());
                                Require(
                                    sent.status == X_Y::SocketIoStatus::Completed &&
                                        sent.value == reply.size(),
                                    "TCP Reactor server failed to send reply");
                            }
                            catch (...)
                            {
                                handlerError = std::current_exception();
                                io.Stop();
                            }
                        });
                }
                catch (...)
                {
                    handlerError = std::current_exception();
                    io.Stop();
                }
            });

        client.Watch(
            io,
            X_Y::IoInterest::Write,
            [&](X_Y::IoReady ready)
            {
                try
                {
                    if (X_Y::HasIoReady(ready, X_Y::IoReady::Error) ||
                        X_Y::HasIoReady(ready, X_Y::IoReady::Closed))
                        throw std::runtime_error("TCP client Reactor reported failure");
                    if (!X_Y::HasIoReady(ready, X_Y::IoReady::Write))
                        return;

                    const auto sent = client.TrySendSome(request.data(), request.size());
                    Require(
                        sent.status == X_Y::SocketIoStatus::Completed &&
                            sent.value == request.size(),
                        "TCP Reactor client failed to send request");
                    client.Watch(
                        io,
                        X_Y::IoInterest::Read,
                        [&](X_Y::IoReady readReady)
                        {
                            try
                            {
                                if (X_Y::HasIoReady(readReady, X_Y::IoReady::Error) ||
                                    X_Y::HasIoReady(readReady, X_Y::IoReady::Closed))
                                    throw std::runtime_error(
                                        "TCP client read Reactor reported failure");
                                if (!X_Y::HasIoReady(readReady, X_Y::IoReady::Read))
                                    return;

                                std::array<char, 64> buffer{};
                                const auto received =
                                    client.TryReceiveSome(buffer.data(), buffer.size());
                                if (received.status == X_Y::SocketIoStatus::WouldBlock)
                                    return;
                                Require(
                                    received.status == X_Y::SocketIoStatus::Completed &&
                                        received.value &&
                                        std::string(buffer.data(), *received.value) == reply,
                                    "TCP Reactor client received unexpected reply");
                                receivedReply = true;
                                io.Stop();
                            }
                            catch (...)
                            {
                                handlerError = std::current_exception();
                                io.Stop();
                            }
                        });
                }
                catch (...)
                {
                    handlerError = std::current_exception();
                    io.Stop();
                }
            });

        std::exception_ptr runError;
        std::thread eventThread([&]
        {
            try
            {
                io.Run();
            }
            catch (...)
            {
                runError = std::current_exception();
            }
        });

        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(3);
        while (!io.IsStopped() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (!io.IsStopped())
            io.Stop();
        eventThread.join();

        if (runError)
            std::rethrow_exception(runError);
        if (handlerError)
            std::rethrow_exception(handlerError);
        Require(receivedRequest, "TCP Reactor server did not receive request");
        Require(receivedReply, "TCP Reactor client did not receive reply");

        client.Close();
        if (accepted)
            accepted->Close();
        server.Close();
    }

    void TestEndpointCloseRemovesReactorWatch()
    {
        X_Y::DatagramServer server;
        Require(
            server.Bind(ParseAddress("127.0.0.1:0")),
            "Reactor close-cleanup UDP bind failed");
        const auto address = server.GetLocalAddr();
        Require(address.has_value(), "Reactor close-cleanup server has no address");

        X_Y::IoContext io;
        std::atomic<bool> callbackInvoked{false};
        server.Watch(
            io,
            X_Y::IoInterest::Read,
            [&](X_Y::IoReady)
            {
                callbackInvoked.store(true, std::memory_order_release);
            });

        std::exception_ptr runError;
        std::thread eventThread([&]
        {
            try
            {
                io.Run();
            }
            catch (...)
            {
                runError = std::current_exception();
            }
        });

        server.Close();
        X_Y::DatagramClient sender;
        const char message = 'x';
        Require(sender.SendTo(*address, &message, 1) == 1,
                "Reactor close-cleanup UDP send failed");
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        io.Stop();
        eventThread.join();

        if (runError)
            std::rethrow_exception(runError);
        Require(
            !callbackInvoked.load(std::memory_order_acquire),
            "Endpoint callback ran after Close removed its Reactor watch");
    }

    void TestTimeoutValidation()
    {
        X_Y::DatagramClient client;
        bool rejectedNegative = false;
        try
        {
            (void)client.SetTimeout(
                X_Y::SocketTimeoutDirection::Receive,
                std::chrono::milliseconds(-1));
        }
        catch (const std::invalid_argument &)
        {
            rejectedNegative = true;
        }
        Require(rejectedNegative, "negative socket timeout was accepted");

        bool rejectedOversized = false;
        try
        {
            (void)client.SetTimeout(
                X_Y::SocketTimeoutDirection::Send,
                std::chrono::milliseconds(
                    static_cast<std::chrono::milliseconds::rep>(
                        std::numeric_limits<std::uint32_t>::max()) +
                    1));
        }
        catch (const std::out_of_range &)
        {
            rejectedOversized = true;
        }
        Require(rejectedOversized, "out-of-range socket timeout was accepted");

        bool rejectedDirection = false;
        try
        {
            (void)client.SetTimeout(
                static_cast<X_Y::SocketTimeoutDirection>(255),
                std::chrono::milliseconds(1));
        }
        catch (const std::invalid_argument &)
        {
            rejectedDirection = true;
        }
        Require(rejectedDirection, "unsupported socket timeout direction was accepted");
    }

#ifdef _WIN32
    void TestTcpReceiveTimeout()
    {
        {
            X_Y::StreamServer server;
            Require(server.Listen(ParseAddress("127.0.0.1:0")), "TCP timeout listen failed");
            Require(
                server.SetTimeout(
                    X_Y::SocketTimeoutDirection::Receive,
                    std::chrono::milliseconds(100)),
                "TCP server receive timeout configuration failed");

            const auto localAddress = server.GetLocalAddr();
            Require(localAddress.has_value(), "TCP timeout server has no local address");

            X_Y::StreamClient client;
            client.Connect("127.0.0.1", localAddress->port);

            std::exception_ptr serverError;
            bool serverTimedOut = false;
            std::chrono::steady_clock::duration serverWait{};
            std::thread serverThread([&]
            {
                try
                {
                    auto connection = server.Accept();
                    Require(connection.has_value(), "TCP timeout accept returned no connection");

                    std::array<char, 8> buffer{};
                    const auto started = std::chrono::steady_clock::now();
                    try
                    {
                        (void)connection->ReceiveSome(buffer.data(), buffer.size());
                    }
                    catch (const X_Y::NetworkTimeoutError &)
                    {
                        serverWait = std::chrono::steady_clock::now() - started;
                        serverTimedOut = true;
                    }
                }
                catch (...)
                {
                    serverError = std::current_exception();
                }
            });
            serverThread.join();
            client.Close();

            if (serverError)
                std::rethrow_exception(serverError);
            Require(serverTimedOut, "accepted TCP connection did not inherit server timeout");
            Require(
                serverWait >= std::chrono::milliseconds(50),
                "accepted TCP receive returned before the configured timeout");
            Require(
                serverWait < std::chrono::seconds(3),
                "accepted TCP receive exceeded the timeout test's safety bound");
        }

        {
            X_Y::StreamServer server;
            Require(server.Listen(ParseAddress("127.0.0.1:0")), "TCP client timeout listen failed");
            const auto localAddress = server.GetLocalAddr();
            Require(localAddress.has_value(), "TCP client timeout server has no local address");

            X_Y::StreamClient client;
            Require(
                client.SetTimeout(
                    X_Y::SocketTimeoutDirection::Receive,
                    std::chrono::milliseconds(100)),
                "TCP client receive timeout configuration failed");
            client.Connect("127.0.0.1", localAddress->port);

            std::exception_ptr serverError;
            std::thread serverThread([&]
            {
                try
                {
                    auto connection = server.Accept();
                    Require(connection.has_value(), "TCP client timeout accept failed");
                    std::this_thread::sleep_for(std::chrono::milliseconds(300));
                }
                catch (...)
                {
                    serverError = std::current_exception();
                }
            });

            std::array<char, 8> buffer{};
            const auto started = std::chrono::steady_clock::now();
            bool clientTimedOut = false;
            int clientErrorCode = 0;
            std::string clientOperation;
            std::exception_ptr clientError;
            try
            {
                (void)client.ReceiveSome(buffer.data(), buffer.size());
            }
            catch (const X_Y::NetworkTimeoutError &error)
            {
                clientTimedOut = true;
                clientErrorCode = error.ErrorCode();
                clientOperation = error.Operation();
            }
            catch (...)
            {
                clientError = std::current_exception();
            }

            const auto elapsed = std::chrono::steady_clock::now() - started;
            client.Close();
            serverThread.join();

            if (clientError)
                std::rethrow_exception(clientError);
            if (serverError)
                std::rethrow_exception(serverError);
            Require(clientTimedOut, "TCP client receive did not report NetworkTimeoutError");
            Require(clientErrorCode != 0, "TCP timeout did not preserve the system error code");
            Require(clientOperation == "recv", "TCP timeout reported the wrong operation");
            Require(
                elapsed >= std::chrono::milliseconds(50),
                "TCP client receive returned before the configured timeout");
            Require(
                elapsed < std::chrono::seconds(3),
                "TCP client receive exceeded the timeout test's safety bound");
        }
    }

#endif

    void TestReceiveTimeout()
    {
        X_Y::DatagramServer server;
        Require(server.Bind(ParseAddress("127.0.0.1:0")), "UDP timeout test bind failed");
        Require(
            server.SetTimeout(
                X_Y::SocketTimeoutDirection::Receive,
                std::chrono::milliseconds(80)),
            "UDP receive timeout configuration failed");

        std::array<char, 8> buffer{};
        const auto started = std::chrono::steady_clock::now();
        bool timedOut = false;
        try
        {
            (void)server.ReceiveFrom(buffer.data(), buffer.size());
        }
        catch (const X_Y::NetworkTimeoutError &)
        {
            timedOut = true;
        }

        const auto elapsed = std::chrono::steady_clock::now() - started;
        Require(timedOut, "UDP receive did not report NetworkTimeoutError");
        Require(
            elapsed >= std::chrono::milliseconds(40),
            "UDP receive returned before the configured timeout");
        Require(
            elapsed < std::chrono::seconds(3),
            "UDP receive exceeded the timeout test's safety bound");

        X_Y::DatagramClient client;
        const auto localAddress = server.GetLocalAddr();
        Require(localAddress.has_value(), "UDP timeout server has no local address");
        const char message = 'x';
        Require(client.SendTo(*localAddress, &message, 1) == 1, "UDP timeout reset send failed");
        Require(
            server.SetTimeout(
                X_Y::SocketTimeoutDirection::Receive,
                std::chrono::milliseconds(0)),
            "UDP receive timeout reset failed");
        const auto received = server.ReceiveFrom(buffer.data(), buffer.size());
        Require(received.size == 1 && buffer[0] == message,
                "zero timeout did not restore an unbounded receive");
    }
}

int main()
{
    try
    {
        TestAddressParsing();
        TestTcpLoopback();
        TestUdpLoopback();
        TestTimeoutValidation();
        TestReactorUdpReadWriteAndStop();
        TestReactorStopFromAnotherThread();
        TestNonBlockingSocketOperations();
        TestEndpointReactorAndTryOperations();
        TestTcpEndpointReactorEcho();
        TestEndpointCloseRemovesReactorWatch();
#ifdef _WIN32
        TestTcpReceiveTimeout();
#endif
        TestReceiveTimeout();
        std::cout << "Network smoke tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "Network smoke tests failed: " << error.what() << '\n';
        return 1;
    }
}
