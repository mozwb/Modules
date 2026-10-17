#pragma once

#include "XCore/Memory/Buffer.h"
#include "Network/NetworkError.h"

#include <cstdint>
#include <string>

namespace X_Y
{
    // Synchronous, blocking TCP client. Network failures throw NetworkError.
    class TcpClientImpl;

    class TcpClient
    {
    public:
        TcpClient();
        ~TcpClient();

        TcpClient(const TcpClient &) = delete;
        TcpClient &operator=(const TcpClient &) = delete;
        TcpClient(TcpClient &&other) noexcept;
        TcpClient &operator=(TcpClient &&other) noexcept;

        void Connect(const std::string &host, std::uint16_t port);

        // TCP is a byte stream; separate calls do not create message boundaries.
        void SendAll(const Buffer &buffer);

        // Reads into the buffer up to its capacity and sets its size to bytes received.
        // Returns 0 for an empty buffer or peer shutdown.
        std::uint64_t ReceiveSome(Buffer &buffer);

        void Close() noexcept;
        bool IsConnected() const noexcept;

    private:
        TcpClientImpl *m_Impl = nullptr;
    };
}
