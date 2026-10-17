#include "Network/TcpClient.h"
#include "TcpClientImpl.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace X_Y
{
    NetworkError::NetworkError(
        std::string operation,
        int errorCode,
        std::string detail)
        : std::runtime_error(
              operation + " failed (error " + std::to_string(errorCode) + "): " + detail),
          m_Operation(std::move(operation)),
          m_ErrorCode(errorCode)
    {
    }

    TcpClient::TcpClient()
        : m_Impl(CreateTcpClientImpl())
    {
        if (m_Impl == nullptr)
            throw std::runtime_error("TCP client factory returned no implementation");
    }

    TcpClient::~TcpClient()
    {
        delete m_Impl;
    }

    TcpClient::TcpClient(TcpClient &&other) noexcept
        : m_Impl(std::exchange(other.m_Impl, nullptr))
    {
    }

    TcpClient &TcpClient::operator=(TcpClient &&other) noexcept
    {
        if (this != &other)
        {
            delete m_Impl;
            m_Impl = std::exchange(other.m_Impl, nullptr);
        }
        return *this;
    }

    void TcpClient::Connect(const std::string &host, std::uint16_t port)
    {
        if (m_Impl == nullptr)
            throw std::logic_error("TCP client has no implementation");
        m_Impl->Connect(host, port);
    }

    void TcpClient::SendAll(const Buffer &buffer)
    {
        if (m_Impl == nullptr)
            throw std::logic_error("TCP client has no implementation");
        m_Impl->SendAll(buffer);
    }

    std::uint64_t TcpClient::ReceiveSome(Buffer &buffer)
    {
        if (m_Impl == nullptr)
            throw std::logic_error("TCP client has no implementation");
        return m_Impl->ReceiveSome(buffer);
    }

    void TcpClient::Close() noexcept
    {
        if (m_Impl != nullptr)
            m_Impl->Close();
    }

    bool TcpClient::IsConnected() const noexcept
    {
        return m_Impl != nullptr && m_Impl->IsConnected();
    }
}
