#pragma once

#include "Network/NetworkError.h"
#include "Network/SocketHandle.h"
#include "Network/IoContext.h"
#include "Memory/Memory.h"

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace X_Y
{
    class StreamTransport
    {
    public:
        virtual ~StreamTransport() = default;

        virtual void Connect(const std::string &host, std::uint16_t port) = 0;
        virtual SocketConnectStatus ConnectNonBlocking(const SocketAddr &)
        {
            throw std::logic_error(
                "Stream transport does not support non-blocking connect");
        }
        virtual SocketConnectStatus FinishConnect()
        {
            throw std::logic_error(
                "Stream transport does not support non-blocking connect");
        }
        virtual bool IsConnecting() const noexcept { return false; }
        virtual bool SetTimeout(
            SocketTimeoutDirection,
            std::chrono::milliseconds)
        {
            // Injected transports may opt out; callers receive false.
            return false;
        }
        virtual std::size_t SendSome(const void *data, std::size_t size) = 0;
        virtual std::size_t ReceiveSome(void *data, std::size_t capacity) = 0;
        virtual SocketIoResult<std::size_t> TrySendSome(const void *, std::size_t)
        {
            throw std::logic_error("Stream transport does not support non-blocking send");
        }
        virtual SocketIoResult<std::size_t> TryReceiveSome(void *, std::size_t)
        {
            throw std::logic_error("Stream transport does not support non-blocking receive");
        }
        virtual IoContext::Registration Watch(
            IoContext &,
            IoInterest,
            IoContext::Handler)
        {
            throw std::logic_error("Stream transport does not support Reactor registration");
        }
        virtual void Close() noexcept = 0;
        virtual bool IsConnected() const noexcept = 0;
        virtual std::optional<SocketAddr> GetLocalAddr() const = 0;
        virtual std::optional<SocketAddr> GetPeerAddr() const = 0;
    };

    class StreamServerTransport
    {
    public:
        virtual ~StreamServerTransport() = default;

        virtual bool Listen(const SocketAddr &address, int backlog) = 0;
        virtual bool SetTimeout(
            SocketTimeoutDirection,
            std::chrono::milliseconds)
        {
            return false;
        }
        virtual std::unique_ptr<StreamTransport> Accept() = 0;
        virtual SocketIoResult<std::unique_ptr<StreamTransport>> TryAccept()
        {
            throw std::logic_error("Stream server transport does not support non-blocking accept");
        }
        virtual IoContext::Registration Watch(
            IoContext &,
            IoInterest,
            IoContext::Handler)
        {
            throw std::logic_error(
                "Stream server transport does not support Reactor registration");
        }
        virtual void Close() noexcept = 0;
        virtual bool IsListening() const noexcept = 0;
        virtual std::optional<SocketAddr> GetLocalAddr() const = 0;
    };

    class DatagramTransport
    {
    public:
        virtual ~DatagramTransport() = default;

        virtual bool Bind(const SocketAddr &address) = 0;
        virtual bool SetTimeout(
            SocketTimeoutDirection,
            std::chrono::milliseconds)
        {
            return false;
        }
        virtual std::size_t SendTo(
            const SocketAddr &peer,
            const void *data,
            std::size_t size) = 0;
        virtual DatagramReceiveResult ReceiveFrom(void *data, std::size_t capacity) = 0;
        virtual SocketIoResult<std::size_t> TrySendTo(
            const SocketAddr &,
            const void *,
            std::size_t)
        {
            throw std::logic_error("Datagram transport does not support non-blocking send");
        }
        virtual SocketIoResult<DatagramReceiveResult> TryReceiveFrom(void *, std::size_t)
        {
            throw std::logic_error("Datagram transport does not support non-blocking receive");
        }
        virtual IoContext::Registration Watch(
            IoContext &,
            IoInterest,
            IoContext::Handler)
        {
            throw std::logic_error("Datagram transport does not support Reactor registration");
        }
        virtual void Close() noexcept = 0;
        virtual bool IsValid() const noexcept = 0;
        virtual bool IsBound() const noexcept = 0;
        virtual std::optional<SocketAddr> GetLocalAddr() const = 0;
    };

    // Default stream transport uses TCP; applications may inject another stream transport.
    std::unique_ptr<StreamTransport> CreateDefaultStreamTransport();
    std::unique_ptr<StreamServerTransport> CreateDefaultStreamServerTransport();
    // Default datagram transport uses UDP; applications may inject another datagram transport.
    std::unique_ptr<DatagramTransport> CreateDefaultDatagramTransport(AddressFamily family);

    class StreamConnection
    {
    public:
        // Stream operations preserve byte-stream semantics; calls do not define messages.
        StreamConnection(StreamConnection &&) noexcept = default;
        StreamConnection &operator=(StreamConnection &&other) noexcept
        {
            if (this != &other)
            {
                m_Registration.Reset();
                m_Transport = std::move(other.m_Transport);
                m_Registration = std::move(other.m_Registration);
                m_ReactorMode = other.m_ReactorMode;
            }
            return *this;
        }

        std::size_t SendSome(const void *data, std::size_t size);
        std::size_t ReceiveSome(void *data, std::size_t capacity);
        SocketIoResult<std::size_t> TrySendSome(const void *data, std::size_t size);
        SocketIoResult<std::size_t> TryReceiveSome(void *data, std::size_t capacity);
        void Watch(IoContext &context, IoInterest interest, IoContext::Handler handler);
        // Change interest after Watch(), for example to stop monitoring writes when a queue drains.
        bool UpdateInterest(IoInterest interest);
        bool SetTimeout(
            SocketTimeoutDirection direction,
            std::chrono::milliseconds timeout);
        void SendAll(const void *data, std::size_t size);
        void SendAll(const Buffer &buffer);
        std::uint64_t ReceiveSome(Buffer &buffer);

        void Close() noexcept
        {
            m_Registration.Reset();
            if (m_Transport)
                m_Transport->Close();
        }
        bool IsConnected() const noexcept
        {
            return m_Transport && m_Transport->IsConnected();
        }
        std::optional<SocketAddr> GetLocalAddr() const
        {
            return m_Transport ? m_Transport->GetLocalAddr() : std::nullopt;
        }
        std::optional<SocketAddr> GetPeerAddr() const
        {
            return m_Transport ? m_Transport->GetPeerAddr() : std::nullopt;
        }

    private:
        friend class StreamClient;
        friend class StreamServer;

        explicit StreamConnection(std::unique_ptr<StreamTransport> transport)
            : m_Transport(std::move(transport)) {}

        std::unique_ptr<StreamTransport> m_Transport;
        IoContext::Registration m_Registration;
        bool m_ReactorMode = false;
    };

    class StreamClient
    {
    public:
        // Inject a custom stream transport, or use the default TCP transport.
        StreamClient();
        explicit StreamClient(std::unique_ptr<StreamTransport> transport);
        StreamClient(const StreamClient &) = delete;
        StreamClient &operator=(const StreamClient &) = delete;
        StreamClient(StreamClient &&) noexcept = default;
        StreamClient &operator=(StreamClient &&other) noexcept
        {
            if (this != &other)
            {
                m_Registration.Reset();
                m_Transport = std::move(other.m_Transport);
                m_Registration = std::move(other.m_Registration);
                m_ReactorMode = other.m_ReactorMode;
            }
            return *this;
        }

        void Connect(const std::string &host, std::uint16_t port);
        // Starts an IP-address connection without blocking; finish it after writable readiness.
        SocketConnectStatus ConnectNonBlocking(const SocketAddr &address);
        // Call after writable readiness; connection errors are reported as NetworkError.
        SocketConnectStatus FinishConnect();
        bool SetTimeout(
            SocketTimeoutDirection direction,
            std::chrono::milliseconds timeout);
        std::size_t SendSome(const void *data, std::size_t size);
        std::size_t ReceiveSome(void *data, std::size_t capacity);
        SocketIoResult<std::size_t> TrySendSome(const void *data, std::size_t size);
        SocketIoResult<std::size_t> TryReceiveSome(void *data, std::size_t capacity);
        void Watch(IoContext &context, IoInterest interest, IoContext::Handler handler);
        // Change interest after Watch(), for example to stop monitoring writes when a queue drains.
        bool UpdateInterest(IoInterest interest);
        void SendAll(const void *data, std::size_t size);
        void SendAll(const Buffer &buffer);
        std::uint64_t ReceiveSome(Buffer &buffer);

        void Close() noexcept;
        bool IsConnected() const noexcept;
        std::optional<SocketAddr> GetLocalAddr() const;
        std::optional<SocketAddr> GetPeerAddr() const;

    private:
        std::unique_ptr<StreamTransport> m_Transport;
        IoContext::Registration m_Registration;
        bool m_ReactorMode = false;
    };

    class StreamServer
    {
    public:
        // Inject a custom stream server transport, or use the default TCP transport.
        StreamServer();
        explicit StreamServer(std::unique_ptr<StreamServerTransport> transport);
        StreamServer(const StreamServer &) = delete;
        StreamServer &operator=(const StreamServer &) = delete;
        StreamServer(StreamServer &&) noexcept = default;
        StreamServer &operator=(StreamServer &&other) noexcept
        {
            if (this != &other)
            {
                m_Registration.Reset();
                m_Transport = std::move(other.m_Transport);
                m_Registration = std::move(other.m_Registration);
                m_ReactorMode = other.m_ReactorMode;
            }
            return *this;
        }

        bool Listen(const SocketAddr &address, int backlog = 128);
        // Applies to accepted connections, not to the blocking Accept operation.
        bool SetTimeout(
            SocketTimeoutDirection direction,
            std::chrono::milliseconds timeout);
        std::optional<StreamConnection> Accept();
        SocketIoResult<StreamConnection> TryAccept();
        void Watch(IoContext &context, IoInterest interest, IoContext::Handler handler);
        void Close() noexcept;
        bool IsListening() const noexcept;
        std::optional<SocketAddr> GetLocalAddr() const;

    private:
        std::unique_ptr<StreamServerTransport> m_Transport;
        IoContext::Registration m_Registration;
        bool m_ReactorMode = false;
    };

    struct StreamSendQueueFlushResult
    {
        SocketIoStatus status = SocketIoStatus::Completed;
        std::size_t bytesSent = 0;
        std::size_t pendingBytes = 0;
    };

    class StreamSendQueue
    {
    public:
        // Not thread-safe. Capacity is fixed; enqueue accepts all bytes or leaves the queue unchanged.
        explicit StreamSendQueue(std::size_t capacityBytes);

        bool Enqueue(const void *data, std::size_t size);
        // Flushes at most byteBudget bytes; pending data remains queued on WouldBlock.
        StreamSendQueueFlushResult Flush(
            StreamConnection &connection,
            std::size_t byteBudget = 64 * 1024);
        StreamSendQueueFlushResult Flush(
            StreamClient &client,
            std::size_t byteBudget = 64 * 1024);

        std::size_t CapacityBytes() const noexcept { return m_CapacityBytes; }
        std::size_t PendingBytes() const noexcept { return m_PendingBytes; }
        bool Empty() const noexcept { return m_PendingBytes == 0; }

    private:
        using SendOperation =
            std::function<SocketIoResult<std::size_t>(const void *, std::size_t)>;

        StreamSendQueueFlushResult FlushWith(
            const SendOperation &send,
            std::size_t byteBudget);

        std::size_t m_CapacityBytes;
        std::size_t m_PendingBytes = 0;
        std::deque<std::vector<std::byte>> m_Chunks;
        std::size_t m_FrontOffset = 0;
    };

    class DatagramClient
    {
    public:
        // Inject a custom datagram transport, or use the default UDP transport.
        explicit DatagramClient(AddressFamily family = AddressFamily::IPv4)
            : m_Transport(CreateDefaultDatagramTransport(family)) {}
        explicit DatagramClient(std::unique_ptr<DatagramTransport> transport);
        DatagramClient(const DatagramClient &) = delete;
        DatagramClient &operator=(const DatagramClient &) = delete;
        DatagramClient(DatagramClient &&) noexcept = default;
        DatagramClient &operator=(DatagramClient &&other) noexcept
        {
            if (this != &other)
            {
                m_Registration.Reset();
                m_Transport = std::move(other.m_Transport);
                m_Registration = std::move(other.m_Registration);
                m_ReactorMode = other.m_ReactorMode;
            }
            return *this;
        }

        bool Bind(const SocketAddr &address);
        bool SetTimeout(
            SocketTimeoutDirection direction,
            std::chrono::milliseconds timeout);
        // Sends exactly one datagram; the returned size is the number of bytes sent.
        std::size_t SendTo(const SocketAddr &peer, const void *data, std::size_t size);
        SocketIoResult<std::size_t> TrySendTo(
            const SocketAddr &peer,
            const void *data,
            std::size_t size);
        void Watch(IoContext &context, IoInterest interest, IoContext::Handler handler);
        // Reports the sender and whether the supplied buffer truncated the datagram.
        DatagramReceiveResult ReceiveFrom(void *data, std::size_t capacity);
        SocketIoResult<DatagramReceiveResult> TryReceiveFrom(
            void *data,
            std::size_t capacity);
        void Close() noexcept;
        bool IsValid() const noexcept;
        std::optional<SocketAddr> GetLocalAddr() const;

    private:
        std::unique_ptr<DatagramTransport> m_Transport;
        IoContext::Registration m_Registration;
        bool m_ReactorMode = false;
    };

    class DatagramServer
    {
    public:
        // Inject a custom datagram transport, or use the default UDP transport.
        explicit DatagramServer(AddressFamily family = AddressFamily::IPv4)
            : m_Transport(CreateDefaultDatagramTransport(family)) {}
        explicit DatagramServer(std::unique_ptr<DatagramTransport> transport);
        DatagramServer(const DatagramServer &) = delete;
        DatagramServer &operator=(const DatagramServer &) = delete;
        DatagramServer(DatagramServer &&) noexcept = default;
        DatagramServer &operator=(DatagramServer &&other) noexcept
        {
            if (this != &other)
            {
                m_Registration.Reset();
                m_Transport = std::move(other.m_Transport);
                m_Registration = std::move(other.m_Registration);
                m_ReactorMode = other.m_ReactorMode;
            }
            return *this;
        }

        bool Bind(const SocketAddr &address);
        bool SetTimeout(
            SocketTimeoutDirection direction,
            std::chrono::milliseconds timeout);
        // Sends exactly one datagram; the returned size is the number of bytes sent.
        std::size_t SendTo(const SocketAddr &peer, const void *data, std::size_t size);
        SocketIoResult<std::size_t> TrySendTo(
            const SocketAddr &peer,
            const void *data,
            std::size_t size);
        void Watch(IoContext &context, IoInterest interest, IoContext::Handler handler);
        // Reports the sender and whether the supplied buffer truncated the datagram.
        DatagramReceiveResult ReceiveFrom(void *data, std::size_t capacity);
        SocketIoResult<DatagramReceiveResult> TryReceiveFrom(
            void *data,
            std::size_t capacity);
        void Close() noexcept;
        bool IsBound() const noexcept;
        std::optional<SocketAddr> GetLocalAddr() const;

    private:
        std::unique_ptr<DatagramTransport> m_Transport;
        IoContext::Registration m_Registration;
        bool m_ReactorMode = false;
    };
}
