#pragma once

#include "Network/TcpClient.h"

namespace X_Y
{
    class TcpClientImpl
    {
    public:
        virtual ~TcpClientImpl() = default;

        virtual void Connect(const std::string &host, std::uint16_t port) = 0;
        virtual void SendAll(const Buffer &buffer) = 0;
        virtual std::uint64_t ReceiveSome(Buffer &buffer) = 0;
        virtual void Close() noexcept = 0;
        virtual bool IsConnected() const noexcept = 0;
    };

    TcpClientImpl *CreateTcpClientImpl();
}
