#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <cstring>

namespace X_Y
{
    using SocketLen = std::uint32_t;
    // Returned by GetNativeHandle when the socket is closed or unavailable.
    inline constexpr std::uintptr_t InvalidNativeSocketHandle =
        static_cast<std::uintptr_t>(-1);

    enum class AddressFamily : uint8_t
    {
        IPv4,
        IPv6
    };

    enum class SocketType : uint8_t
    {
        Stream,
        Datagram
    };

    // 我们自己定义的Socket选项ID，上层只使用这个枚举
    enum class SocketOptionId : uint8_t
    {
        ReuseAddr,
        TcpNoDelay,
        RecvBuf,
        SendBuf,
    };

    struct SocketAddr
    {
        AddressFamily family{};
        std::uint8_t ipBytes[16]{}; // IPv4只用前4字节；IPv6占满16字节
        std::uint16_t port{};
        std::uint32_t scopeId = 0; // IPv6链路本地地址使用，大部分场景为0

        // 核心：从字符串解析 "127.0.0.1:8080" / "[::1]:8080"
        static std::optional<SocketAddr> Parse(std::string_view str);
        // 序列化为字符串：自动生成对应格式
        std::string ToString() const;

        // 比较运算符
        bool operator==(const SocketAddr &other) const
        {
            if (family != other.family || port != other.port)
                return false;

            if (family == AddressFamily::IPv4)
                return std::memcmp(ipBytes, other.ipBytes, 4) == 0;

            return scopeId == other.scopeId &&
                   std::memcmp(ipBytes, other.ipBytes, sizeof(ipBytes)) == 0;
        }

        bool operator!=(const SocketAddr &other) const
        {
            return !(*this == other);
        }
    };

    struct DatagramReceiveResult
    {
        std::size_t size = 0;
        SocketAddr peer;
        bool truncated = false;
    };

    class SocketHandleImpl
    {
    public:
        virtual ~SocketHandleImpl() = default;

        // Bind/listen/configuration report false on failure; transport errors throw NetworkError.
        virtual bool Bind(const SocketAddr &addr) = 0;
        virtual void Connect(const SocketAddr &addr) = 0;
        virtual bool Listen(int backlog) = 0;
        virtual std::unique_ptr<SocketHandleImpl> Accept() = 0;
        virtual std::size_t SendSome(const void *data, std::size_t size) = 0;
        virtual std::size_t ReceiveSome(void *data, std::size_t size) = 0;
        virtual std::size_t SendTo(const SocketAddr &peer, const void *data, std::size_t size) = 0;
        virtual DatagramReceiveResult ReceiveFrom(void *data, std::size_t capacity) = 0;
        virtual bool SetNonBlocking(bool enable) = 0;
        // Option values are passed as int; optlen must equal sizeof(int).
        virtual bool SetOption(SocketOptionId option, const void *val, SocketLen optlen) = 0;
        virtual std::optional<SocketAddr> GetLocalAddr() const = 0;
        virtual std::optional<SocketAddr> GetPeerAddr() const = 0;
        virtual void Close() noexcept = 0;
        virtual bool IsValid() const = 0;
        virtual std::uintptr_t GetNativeHandle() const = 0;
    };

    SocketHandleImpl *CreateSocketHandleImpl(AddressFamily family, SocketType type);
}