#pragma once

#include "Network/NetworkError.h"
#include "Network/SocketHandle.h"
#include "XCore/Memory/Buffer.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace X_Y
{
    class StreamTransport
    {
    public:
        virtual ~StreamTransport() = default;

        virtual void Connect(const std::string &host, std::uint16_t port) = 0;
        virtual std::size_t SendSome(const void *data, std::size_t size) = 0;
        virtual std::size_t ReceiveSome(void *data, std::size_t capacity) = 0;
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
        virtual std::unique_ptr<StreamTransport> Accept() = 0;
        virtual void Close() noexcept = 0;
        virtual bool IsListening() const noexcept = 0;
        virtual std::optional<SocketAddr> GetLocalAddr() const = 0;
    };

    class DatagramTransport
    {
    public:
        virtual ~DatagramTransport() = default;

        virtual bool Bind(const SocketAddr &address) = 0;
        virtual std::size_t SendTo(
            const SocketAddr &peer,
            const void *data,
            std::size_t size) = 0;
        virtual DatagramReceiveResult ReceiveFrom(void *data, std::size_t capacity) = 0;
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
        StreamConnection &operator=(StreamConnection &&) noexcept = default;

        std::size_t SendSome(const void *data, std::size_t size);
        std::size_t ReceiveSome(void *data, std::size_t capacity);
        void SendAll(const void *data, std::size_t size);
        void SendAll(const Buffer &buffer);
        std::uint64_t ReceiveSome(Buffer &buffer);

        void Close() noexcept
        {
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
        StreamClient &operator=(StreamClient &&) noexcept = default;

        void Connect(const std::string &host, std::uint16_t port);
        std::size_t SendSome(const void *data, std::size_t size);
        std::size_t ReceiveSome(void *data, std::size_t capacity);
        void SendAll(const void *data, std::size_t size);
        void SendAll(const Buffer &buffer);
        std::uint64_t ReceiveSome(Buffer &buffer);

        void Close() noexcept;
        bool IsConnected() const noexcept;
        std::optional<SocketAddr> GetLocalAddr() const;
        std::optional<SocketAddr> GetPeerAddr() const;

    private:
        std::unique_ptr<StreamTransport> m_Transport;
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
        StreamServer &operator=(StreamServer &&) noexcept = default;

        bool Listen(const SocketAddr &address, int backlog = 128);
        std::optional<StreamConnection> Accept();
        void Close() noexcept;
        bool IsListening() const noexcept;
        std::optional<SocketAddr> GetLocalAddr() const;

    private:
        std::unique_ptr<StreamServerTransport> m_Transport;
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
        DatagramClient &operator=(DatagramClient &&) noexcept = default;

        bool Bind(const SocketAddr &address);
        // Sends exactly one datagram; the returned size is the number of bytes sent.
        std::size_t SendTo(const SocketAddr &peer, const void *data, std::size_t size);
        // Reports the sender and whether the supplied buffer truncated the datagram.
        DatagramReceiveResult ReceiveFrom(void *data, std::size_t capacity);
        void Close() noexcept;
        bool IsValid() const noexcept;
        std::optional<SocketAddr> GetLocalAddr() const;

    private:
        std::unique_ptr<DatagramTransport> m_Transport;
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
        DatagramServer &operator=(DatagramServer &&) noexcept = default;

        bool Bind(const SocketAddr &address);
        // Sends exactly one datagram; the returned size is the number of bytes sent.
        std::size_t SendTo(const SocketAddr &peer, const void *data, std::size_t size);
        // Reports the sender and whether the supplied buffer truncated the datagram.
        DatagramReceiveResult ReceiveFrom(void *data, std::size_t capacity);
        void Close() noexcept;
        bool IsBound() const noexcept;
        std::optional<SocketAddr> GetLocalAddr() const;

    private:
        std::unique_ptr<DatagramTransport> m_Transport;
    };
}
