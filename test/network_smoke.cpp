#include "Network/Endpoints.h"
#include "Network/IoContext.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
    class ScriptedStreamTransport final : public X_Y::StreamTransport
    {
    public:
        void Connect(const std::string &, std::uint16_t) override
        {
            m_Connected = true;
        }

        std::size_t SendSome(const void *, std::size_t) override
        {
            throw std::logic_error("blocking send used by scripted transport");
        }

        std::size_t ReceiveSome(void *, std::size_t) override
        {
            throw std::logic_error("blocking receive used by scripted transport");
        }

        X_Y::SocketIoResult<std::size_t> TrySendSome(
            const void *data,
            std::size_t size) override
        {
            if (m_BlockNextSend)
            {
                m_BlockNextSend = false;
                return {X_Y::SocketIoStatus::WouldBlock, std::nullopt};
            }

            const std::size_t count = std::min(size, m_MaxWrite);
            m_Sent.append(static_cast<const char *>(data), count);
            if (m_BlockAfterSuccessfulSend)
            {
                m_BlockAfterSuccessfulSend = false;
                m_BlockNextSend = true;
            }
            return {X_Y::SocketIoStatus::Completed, count};
        }

        X_Y::SocketIoResult<std::size_t> TryReceiveSome(void *, std::size_t) override
        {
            return {X_Y::SocketIoStatus::WouldBlock, std::nullopt};
        }

        void Close() noexcept override { m_Connected = false; }
        bool IsConnected() const noexcept override { return m_Connected; }
        std::optional<X_Y::SocketAddr> GetLocalAddr() const override
        {
            return std::nullopt;
        }
        std::optional<X_Y::SocketAddr> GetPeerAddr() const override
        {
            return std::nullopt;
        }

        void BlockAfterNextSuccessfulSend() noexcept
        {
            m_BlockAfterSuccessfulSend = true;
        }
        const std::string &Sent() const noexcept { return m_Sent; }

    private:
        bool m_Connected = false;
        bool m_BlockNextSend = false;
        bool m_BlockAfterSuccessfulSend = false;
        std::size_t m_MaxWrite = 2;
        std::string m_Sent;
    };

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

    void TestIoContextTimers()
    {
        X_Y::IoContext io;
        bool cancelledTimerRan = false;
        auto cancelledTimer = io.ScheduleAfter(
            std::chrono::seconds(5),
            [&] { cancelledTimerRan = true; });
        Require(cancelledTimer.Cancel(), "IoContext timer could not be cancelled");
        Require(!cancelledTimer, "cancelled IoContext timer remained active");

        const auto runThread = std::this_thread::get_id();
        std::thread::id callbackThread;
        bool timerRan = false;
        bool timerAfterStopRan = false;
        auto timer = io.ScheduleAfter(
            std::chrono::milliseconds(40),
            [&]
            {
                callbackThread = std::this_thread::get_id();
                timerRan = true;
                io.Stop();
            });
        auto timerAfterStop = io.ScheduleAfter(
            std::chrono::milliseconds(40),
            [&] { timerAfterStopRan = true; });
        io.Run();

        Require(timerRan, "IoContext timer callback did not run");
        Require(!cancelledTimerRan, "cancelled IoContext timer callback ran");
        Require(!timerAfterStopRan, "IoContext ran another timer after Stop");
        Require(
            callbackThread == runThread,
            "IoContext timer callback ran outside the Run thread");
        Require(!timer.Cancel(), "fired IoContext timer was reported as cancelled");

        bool rejectedNegativeDelay = false;
        try
        {
            (void)io.ScheduleAfter(std::chrono::milliseconds(-1), [] {});
        }
        catch (const std::invalid_argument &)
        {
            rejectedNegativeDelay = true;
        }
        Require(rejectedNegativeDelay, "IoContext accepted a negative timer delay");
    }

    void TestIoContextPost()
    {
        X_Y::IoContext io;
        std::atomic<bool> runEntered{false};
        std::thread::id runThread;
        std::thread::id taskThread;
        std::exception_ptr runError;

        std::thread eventThread([&]
        {
            runThread = std::this_thread::get_id();
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
        Require(
            io.Post([&]
            {
                taskThread = std::this_thread::get_id();
                io.Stop();
            }),
            "IoContext rejected a task while running");
        eventThread.join();

        if (runError)
            std::rethrow_exception(runError);
        Require(taskThread == runThread, "posted task did not run on the Run thread");
        Require(!io.Post([] {}), "stopped IoContext accepted a posted task");

        bool rejectedEmptyTask = false;
        try
        {
            (void)io.Post({});
        }
        catch (const std::invalid_argument &)
        {
            rejectedEmptyTask = true;
        }
        Require(rejectedEmptyTask, "IoContext accepted an empty posted task");
    }

    void TestAsyncDnsResolution()
    {
        X_Y::IoContext io;
        std::atomic<bool> cancelledCallbackRan{false};
        auto cancelled = io.ResolveAsync(
            "localhost",
            443,
            [&](std::vector<X_Y::SocketAddr>, std::exception_ptr)
            {
                cancelledCallbackRan.store(true, std::memory_order_release);
            });
        Require(cancelled.Cancel(), "asynchronous DNS request could not be cancelled");
        Require(!cancelled.IsPending(), "cancelled DNS request remained pending");

        std::thread::id runThread;
        std::thread::id callbackThread;
        bool resolved = false;
        std::vector<X_Y::SocketAddr> addresses;
        std::exception_ptr resolutionError;
        auto request = io.ResolveAsync(
            "localhost",
            443,
            [&](std::vector<X_Y::SocketAddr> result, std::exception_ptr error)
            {
                callbackThread = std::this_thread::get_id();
                addresses = std::move(result);
                resolutionError = error;
                resolved = true;
                io.Stop();
            });

        std::exception_ptr runError;
        std::thread eventThread([&]
        {
            runThread = std::this_thread::get_id();
            try
            {
                io.Run();
            }
            catch (...)
            {
                runError = std::current_exception();
            }
        });
        eventThread.join();

        if (runError)
            std::rethrow_exception(runError);
        Require(resolved, "asynchronous DNS callback did not run");
        Require(!cancelledCallbackRan.load(std::memory_order_acquire),
                "cancelled DNS callback ran");
        Require(callbackThread == runThread,
                "asynchronous DNS callback ran outside the Run thread");
        if (resolutionError)
            std::rethrow_exception(resolutionError);
        Require(!addresses.empty(), "localhost DNS resolution returned no addresses");
        for (const auto &address : addresses)
            Require(address.port == 443, "resolved address lost its service port");
        Require(!request.IsPending(), "completed DNS request remained pending");

        bool rejectedEmptyHost = false;
        try
        {
            (void)io.ResolveAsync(
                "",
                80,
                [](std::vector<X_Y::SocketAddr>, std::exception_ptr) {});
        }
        catch (const std::invalid_argument &)
        {
            rejectedEmptyHost = true;
        }
        Require(rejectedEmptyHost, "asynchronous DNS accepted an empty hostname");
    }

    void TestAsyncConnectComposition()
    {
        X_Y::StreamServer server;
        Require(
            server.Listen(ParseAddress("127.0.0.1:0")),
            "async connect composition listener failed");
        const auto address = server.GetLocalAddr();
        Require(address.has_value(), "async connect listener has no address");

        X_Y::IoContext io;
        X_Y::StreamClient client;
        bool connected = false;
        bool timedOut = false;
        std::exception_ptr operationError;
        X_Y::IoContext::Timer timeout;
        auto resolution = io.ResolveAsync(
            "127.0.0.1",
            address->port,
            [&](std::vector<X_Y::SocketAddr> addresses, std::exception_ptr error)
            {
                if (error)
                {
                    operationError = error;
                    io.Stop();
                    return;
                }
                Require(!addresses.empty(), "async connect resolution returned no addresses");

                const auto status = client.ConnectNonBlocking(addresses.front());
                if (status == X_Y::SocketConnectStatus::Connected)
                {
                    connected = true;
                    timeout.Cancel();
                    io.Stop();
                    return;
                }

                client.Watch(
                    io,
                    X_Y::IoInterest::Write,
                    [&](X_Y::IoReady ready)
                    {
                        try
                        {
                            (void)ready;
                            if (client.FinishConnect() ==
                                X_Y::SocketConnectStatus::Connected)
                            {
                                connected = true;
                                timeout.Cancel();
                                io.Stop();
                            }
                        }
                        catch (...)
                        {
                            operationError = std::current_exception();
                            io.Stop();
                        }
                    });
            });
        timeout = io.ScheduleAfter(
            std::chrono::seconds(2),
            [&]
            {
                timedOut = true;
                client.Close();
                io.Stop();
            });

        io.Run();
        if (operationError)
            std::rethrow_exception(operationError);
        Require(connected, "async DNS + non-blocking connect did not connect");
        Require(!timedOut, "async DNS + non-blocking connect timed out");
        Require(server.Accept().has_value(), "async connect was not accepted by server");
        client.Close();
        Require(!resolution.IsPending(), "async connect DNS request remained pending");
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

    void TestNonBlockingTcpConnect()
    {
        X_Y::StreamServer server;
        Require(
            server.Listen(ParseAddress("127.0.0.1:0")),
            "non-blocking connect test listen failed");
        const auto address = server.GetLocalAddr();
        Require(address.has_value(), "non-blocking connect listener has no address");

        X_Y::StreamClient client;
        const auto startStatus = client.ConnectNonBlocking(*address);
        Require(
            startStatus == X_Y::SocketConnectStatus::Connected ||
                startStatus == X_Y::SocketConnectStatus::InProgress,
            "non-blocking connect returned an invalid state");

        X_Y::IoContext io;
        std::exception_ptr handlerError;
        auto finishStatus = startStatus;
        bool interestUpdated = false;
        client.Watch(
            io,
            X_Y::IoInterest::Write,
            [&](X_Y::IoReady ready)
            {
                try
                {
                    if (X_Y::HasIoReady(ready, X_Y::IoReady::Error) ||
                        X_Y::HasIoReady(ready, X_Y::IoReady::Closed))
                    {
                        if (!client.IsConnected())
                        {
                            finishStatus = client.FinishConnect();
                            if (finishStatus == X_Y::SocketConnectStatus::InProgress)
                                throw std::runtime_error(
                                    "connect error event arrived while connection was pending");
                        }
                        else
                        {
                            throw std::runtime_error(
                                "connected socket reported a Reactor error");
                        }
                    }
                    else if (X_Y::HasIoReady(ready, X_Y::IoReady::Write) &&
                             finishStatus == X_Y::SocketConnectStatus::InProgress)
                    {
                        finishStatus = client.FinishConnect();
                    }

                    if (finishStatus == X_Y::SocketConnectStatus::Connected)
                    {
                        interestUpdated =
                            client.UpdateInterest(X_Y::IoInterest::Read);
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
        Require(
            finishStatus == X_Y::SocketConnectStatus::Connected &&
                client.IsConnected(),
            "non-blocking TCP connection did not complete");
        Require(interestUpdated, "connected client could not update Reactor interest");

        auto accepted = server.Accept();
        Require(accepted.has_value(), "server did not accept non-blocking client");
        client.Close();
        accepted->Close();
        server.Close();
    }

    void TestNonBlockingTcpConnectFailure()
    {
        X_Y::StreamServer temporaryServer;
        Require(
            temporaryServer.Listen(ParseAddress("127.0.0.1:0")),
            "connect failure test listen failed");
        const auto refusedAddress = temporaryServer.GetLocalAddr();
        Require(refusedAddress.has_value(), "connect failure test has no local address");
        temporaryServer.Close();

        X_Y::StreamClient client;
        bool observedFailure = false;
        try
        {
            const auto status = client.ConnectNonBlocking(*refusedAddress);
            if (status == X_Y::SocketConnectStatus::InProgress)
            {
                X_Y::IoContext io;
                std::exception_ptr runError;
                client.Watch(
                    io,
                    X_Y::IoInterest::Write,
                    [&](X_Y::IoReady ready)
                    {
                        if (!X_Y::HasIoReady(ready, X_Y::IoReady::Write) &&
                            !X_Y::HasIoReady(ready, X_Y::IoReady::Error) &&
                            !X_Y::HasIoReady(ready, X_Y::IoReady::Closed))
                            return;
                        try
                        {
                            (void)client.FinishConnect();
                        }
                        catch (const X_Y::NetworkError &)
                        {
                            observedFailure = true;
                        }
                        catch (...)
                        {
                            runError = std::current_exception();
                        }
                        io.Stop();
                    });

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
            }
        }
        catch (const X_Y::NetworkError &)
        {
            observedFailure = true;
        }

        Require(observedFailure, "refused non-blocking connect did not report NetworkError");
        Require(!client.IsConnected(), "failed non-blocking connection was reported connected");
        client.Close();
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
        bool requestQueued = false;
        bool replyQueued = false;
        X_Y::StreamSendQueue clientOutgoing(1024);
        X_Y::StreamSendQueue serverOutgoing(1024);
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
                                if (X_Y::HasIoReady(
                                        connectionReady,
                                        X_Y::IoReady::Read))
                                {
                                    std::array<char, 64> buffer{};
                                    const auto received =
                                        accepted->TryReceiveSome(buffer.data(), buffer.size());
                                    if (received.status == X_Y::SocketIoStatus::Completed)
                                    {
                                        Require(
                                            received.value &&
                                                std::string(buffer.data(), *received.value) ==
                                                    request,
                                            "TCP Reactor server received unexpected request");
                                        receivedRequest = true;
                                        Require(
                                            serverOutgoing.Enqueue(
                                                reply.data(),
                                                reply.size()),
                                            "TCP Reactor server send queue is unexpectedly full");
                                        replyQueued = true;
                                        Require(
                                            accepted->UpdateInterest(
                                                X_Y::IoInterest::ReadWrite),
                                            "TCP Reactor server failed to enable write interest");
                                    }
                                    else if (received.status == X_Y::SocketIoStatus::Closed)
                                    {
                                        throw std::runtime_error(
                                            "TCP Reactor server connection closed before request");
                                    }
                                }
                                if (replyQueued &&
                                    X_Y::HasIoReady(
                                        connectionReady,
                                        X_Y::IoReady::Write))
                                {
                                    const auto flushed = serverOutgoing.Flush(*accepted);
                                    if (flushed.status == X_Y::SocketIoStatus::Closed)
                                        throw std::runtime_error(
                                            "TCP Reactor server connection closed while sending");
                                    if (serverOutgoing.Empty())
                                        Require(
                                            accepted->UpdateInterest(X_Y::IoInterest::Read),
                                            "TCP Reactor server failed to disable write interest");
                                }
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

                    if (X_Y::HasIoReady(ready, X_Y::IoReady::Write))
                    {
                        if (!requestQueued)
                        {
                            Require(
                                clientOutgoing.Enqueue(request.data(), request.size()),
                                "TCP Reactor client send queue is unexpectedly full");
                            requestQueued = true;
                        }
                        const auto flushed = clientOutgoing.Flush(client);
                        if (flushed.status == X_Y::SocketIoStatus::Closed)
                            throw std::runtime_error(
                                "TCP Reactor client connection closed while sending");
                        if (clientOutgoing.Empty())
                            Require(
                                client.UpdateInterest(X_Y::IoInterest::Read),
                                "TCP Reactor client failed to disable write interest");
                    }

                    if (X_Y::HasIoReady(ready, X_Y::IoReady::Read))
                    {
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

    void TestStreamSendQueue()
    {
        auto transport = std::make_unique<ScriptedStreamTransport>();
        auto *script = transport.get();
        X_Y::StreamClient client(std::move(transport));
        client.Connect("scripted", 1);
        X_Y::StreamSendQueue queue(8);

        Require(queue.Enqueue("hello", 5), "send queue rejected data within capacity");
        Require(!queue.Enqueue("1234", 4), "send queue accepted data beyond capacity");
        Require(queue.PendingBytes() == 5, "send queue changed after a rejected enqueue");
        Require(queue.Enqueue(nullptr, 0), "zero-length enqueue should be a no-op");

        script->BlockAfterNextSuccessfulSend();
        const auto blocked = queue.Flush(client);
        Require(
            blocked.status == X_Y::SocketIoStatus::WouldBlock &&
                blocked.bytesSent == 2 &&
                blocked.pendingBytes == 3 &&
                script->Sent() == "he",
            "send queue did not preserve data after partial write and WouldBlock");

        const auto flushed = queue.Flush(client);
        Require(
            flushed.status == X_Y::SocketIoStatus::Completed &&
                flushed.bytesSent == 3 &&
                flushed.pendingBytes == 0 &&
                queue.Empty() &&
                script->Sent() == "hello",
            "send queue did not resume and finish a partial write");

        Require(queue.Enqueue("abcdef", 6), "send queue rejected second batch");
        const auto budgeted = queue.Flush(client, 2);
        Require(
            budgeted.status == X_Y::SocketIoStatus::Completed &&
                budgeted.bytesSent == 2 &&
                budgeted.pendingBytes == 4,
            "send queue ignored the per-flush byte budget");
        Require(!queue.Enqueue("12345", 5), "send queue exceeded its configured bound");
        const auto remainder = queue.Flush(client);
        Require(
            remainder.status == X_Y::SocketIoStatus::Completed &&
                remainder.pendingBytes == 0 &&
                script->Sent() == "helloabcdef",
            "send queue did not preserve FIFO order");

        bool rejectedZeroCapacity = false;
        try
        {
            X_Y::StreamSendQueue invalid(0);
        }
        catch (const std::invalid_argument &)
        {
            rejectedZeroCapacity = true;
        }
        Require(rejectedZeroCapacity, "send queue accepted zero capacity");
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
        TestIoContextTimers();
        TestIoContextPost();
        TestAsyncDnsResolution();
        TestAsyncConnectComposition();
        TestNonBlockingSocketOperations();
        TestEndpointReactorAndTryOperations();
        TestNonBlockingTcpConnect();
        TestNonBlockingTcpConnectFailure();
        TestTcpEndpointReactorEcho();
        TestEndpointCloseRemovesReactorWatch();
        TestStreamSendQueue();
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
