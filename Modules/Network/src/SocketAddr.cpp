#include "Network/src/SocketHandleImpl.h"

#include "Network/NetworkError.h"

#include <charconv>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

namespace X_Y
{
    namespace
    {
#ifdef _WIN32
        class WinsockRuntime
        {
        public:
            WinsockRuntime()
            {
                WSADATA data{};
                const int error = WSAStartup(MAKEWORD(2, 2), &data);
                if (error != 0)
                    throw NetworkError("WSAStartup", error, "failed to initialize Winsock");
            }

            ~WinsockRuntime() { WSACleanup(); }
        };
#endif

        void EnsureSocketRuntime()
        {
#ifdef _WIN32
            static WinsockRuntime runtime;
            (void)runtime;
#endif
        }

        bool ParseUnsigned(std::string_view text, std::uint32_t &value)
        {
            if (text.empty())
                return false;

            const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
            return result.ec == std::errc{} && result.ptr == text.data() + text.size();
        }

        std::optional<std::uint16_t> ParsePort(std::string_view text)
        {
            std::uint32_t port = 0;
            if (!ParseUnsigned(text, port) || port > UINT16_MAX)
                return std::nullopt;
            return static_cast<std::uint16_t>(port);
        }
    }

    std::optional<SocketAddr> SocketAddr::Parse(std::string_view str)
    {
        EnsureSocketRuntime();
        if (str.empty())
            return std::nullopt;

        SocketAddr result{};
        std::string address;
        std::string_view portText;

        if (str.front() == '[')
        {
            const std::size_t closeBracket = str.find(']');
            if (closeBracket == std::string_view::npos ||
                closeBracket + 1 >= str.size() ||
                str[closeBracket + 1] != ':')
                return std::nullopt;

            result.family = AddressFamily::IPv6;
            address.assign(str.substr(1, closeBracket - 1));
            portText = str.substr(closeBracket + 2);

            const std::size_t scopeDelimiter = address.find('%');
            if (scopeDelimiter != std::string::npos)
            {
                if (!ParseUnsigned(
                        std::string_view(address).substr(scopeDelimiter + 1),
                        result.scopeId))
                    return std::nullopt;
                address.resize(scopeDelimiter);
            }
        }
        else
        {
            const std::size_t colon = str.rfind(':');
            if (colon == std::string_view::npos ||
                str.find(':') != colon)
                return std::nullopt;

            result.family = AddressFamily::IPv4;
            address.assign(str.substr(0, colon));
            portText = str.substr(colon + 1);
        }

        const auto port = ParsePort(portText);
        if (!port)
            return std::nullopt;
        result.port = *port;

        const int nativeFamily =
            result.family == AddressFamily::IPv4 ? AF_INET : AF_INET6;
        if (inet_pton(nativeFamily, address.c_str(), result.ipBytes) != 1)
            return std::nullopt;
        return result;
    }

    std::string SocketAddr::ToString() const
    {
        EnsureSocketRuntime();

        char address[INET6_ADDRSTRLEN]{};
        const int nativeFamily =
            family == AddressFamily::IPv4 ? AF_INET : family == AddressFamily::IPv6 ? AF_INET6
                                                                                    : 0;
        if (nativeFamily == 0 ||
            inet_ntop(nativeFamily, ipBytes, address, sizeof(address)) == nullptr)
            throw std::invalid_argument("SocketAddr contains an invalid address family or IP");

        std::string result;
        if (family == AddressFamily::IPv6)
        {
            result.reserve(std::char_traits<char>::length(address) + 16);
            result.push_back('[');
            result += address;
            if (scopeId != 0)
            {
                result.push_back('%');
                char scopeBuffer[std::numeric_limits<std::uint32_t>::digits10 + 2]{};
                const auto conversion = std::to_chars(
                    scopeBuffer,
                    scopeBuffer + sizeof(scopeBuffer),
                    scopeId);
                result.append(scopeBuffer, conversion.ptr);
            }
            result += "]:";
        }
        else
        {
            result = address;
            result.push_back(':');
        }

        char portBuffer[std::numeric_limits<std::uint16_t>::digits10 + 2]{};
        const auto conversion = std::to_chars(
            portBuffer,
            portBuffer + sizeof(portBuffer),
            port);
        result.append(portBuffer, conversion.ptr);
        return result;
    }
}