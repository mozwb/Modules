#pragma once

#include "Network/src/SocketHandleImpl.h"

#include <cstdint>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace X_Y
{
    class SocketHandle
    {
    public:
        // Creates a stream or datagram socket for the selected address family.
        explicit SocketHandle(
            AddressFamily family = AddressFamily::IPv4,
            SocketType type = SocketType::Stream);
        ~SocketHandle() = default;

        SocketHandle(const SocketHandle &) = delete;
        SocketHandle &operator=(const SocketHandle &) = delete;
        SocketHandle(SocketHandle &&) noexcept = default;
        SocketHandle &operator=(SocketHandle &&) noexcept = default;

        bool Bind(const SocketAddr &addr)
        {
            return m_Impl && m_Impl->Bind(addr);
        }

        // Connect and data-transfer failures throw NetworkError.
        void Connect(const SocketAddr &addr)
        {
            RequireImpl().Connect(addr);
        }

        bool Listen(int backlog = 128)
        {
            return m_Impl && m_Impl->Listen(backlog);
        }

        std::optional<SocketHandle> Accept()
        {
            if (!m_Impl)
                return std::nullopt;
            auto accepted = m_Impl->Accept();
            if (!accepted)
                return std::nullopt;
            return SocketHandle(std::move(accepted));
        }

        // Performs one send/receive operation; TCP may transfer fewer bytes than requested.
        std::size_t SendSome(const void *data, std::size_t size)
        {
            return RequireImpl().SendSome(data, size);
        }

        // Returns 0 when the peer has shut down its sending side.
        std::size_t ReceiveSome(void *data, std::size_t size)
        {
            return RequireImpl().ReceiveSome(data, size);
        }

        std::size_t SendTo(const SocketAddr &peer, const void *data, std::size_t size)
        {
            return RequireImpl().SendTo(peer, data, size);
        }

        DatagramReceiveResult ReceiveFrom(void *data, std::size_t capacity)
        {
            return RequireImpl().ReceiveFrom(data, capacity);
        }

        bool SetNonBlocking(bool enable)
        {
            return m_Impl && m_Impl->SetNonBlocking(enable);
        }

        bool SetOption(SocketOptionId option, const void *val, SocketLen optlen)
        {
            return m_Impl && m_Impl->SetOption(option, val, optlen);
        }

        std::optional<SocketAddr> GetLocalAddr() const
        {
            return m_Impl ? m_Impl->GetLocalAddr() : std::nullopt;
        }

        std::optional<SocketAddr> GetPeerAddr() const
        {
            return m_Impl ? m_Impl->GetPeerAddr() : std::nullopt;
        }

        void Close() noexcept
        {
            if (m_Impl)
                m_Impl->Close();
        }

        bool IsValid() const
        {
            return m_Impl && m_Impl->IsValid();
        }

        std::uintptr_t GetNativeHandle() const
        {
            return m_Impl ? m_Impl->GetNativeHandle() : InvalidNativeSocketHandle;
        }

    private:
        explicit SocketHandle(std::unique_ptr<SocketHandleImpl> impl)
            : m_Impl(std::move(impl))
        {
        }

        SocketHandleImpl &RequireImpl() const
        {
            if (!m_Impl)
                throw std::logic_error("Socket handle has no implementation");
            return *m_Impl;
        }

        std::unique_ptr<SocketHandleImpl> m_Impl;
    };
}