// ─────────────────────────────────────────────────────────────────────────────
// tcp_echo_server.cpp — 基于 X_Y Network 模块的最小 TCP 回显服务器
//
// 这里不直接调用 socket/bind/listen/accept，而是复用仓库已有的：
//   X_Y::SocketAddr       地址解析 "0.0.0.0:9000" / 格式化
//   X_Y::StreamServer     监听 + Accept
//   X_Y::StreamConnection SendAll / ReceiveSome
//   X_Y::Buffer           收发缓冲
//
// 协议：一行一条消息（'\n' 结尾），回复 "echo: <原消息>\n"。
// 单线程、一次服务一个客户端；并发留作下一步练习。
//
// 构建：见同目录 CMakeLists.txt（会一起编译 Network + Buffer/Memory 源码）
// 运行：./echo_server [port]       默认 9000
// ─────────────────────────────────────────────────────────────────────────────

#include "Network/Endpoints.h"
#include "Network/NetworkError.h"
#include "XCore/Memory/Buffer.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace
{
    constexpr std::uint64_t kReadCapacity = 4096;
    constexpr std::size_t kMaxPendingBytes = 1024 * 1024; // 防止只发不收撑爆内存

    void ServeClient(X_Y::StreamConnection &connection, unsigned long long clientId)
    {
        X_Y::Buffer chunk(kReadCapacity);
        if (!chunk)
            throw std::runtime_error("failed to allocate receive buffer");

        X_Y::Buffer reply;
        std::string pending; // 还没凑够一整行的字节

        for (;;)
        {
            const std::uint64_t received = connection.ReceiveSome(chunk);
            if (received == 0)
            {
                std::printf("[server] client #%llu closed the connection\n", clientId);
                return;
            }

            pending.append(reinterpret_cast<const char *>(chunk.Data),
                           static_cast<std::size_t>(chunk.Size));
            if (pending.size() > kMaxPendingBytes)
                throw std::runtime_error("client sent too much data without a newline");

            // TCP 是字节流：一次 ReceiveSome 可能只有半行，也可能包含多行。
            std::size_t consumed = 0;
            for (;;)
            {
                const std::size_t newline = pending.find('\n', consumed);
                if (newline == std::string::npos)
                    break;

                std::string line = pending.substr(consumed, newline - consumed);
                consumed = newline + 1;
                if (!line.empty() && line.back() == '\r') // 兼容 Windows 客户端
                    line.pop_back();

                std::printf("[server] client #%llu <- %s\n", clientId, line.c_str());

                reply.Overwrite("echo: ");
                reply.Append(line.data(), line.size());
                reply.Append("\n", 1);
                connection.SendAll(reply); // SendAll 内部会处理短写
            }
            pending.erase(0, consumed);
        }
    }
}

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    std::uint16_t port = 9000;
    if (argc > 1)
    {
        const int parsed = std::atoi(argv[1]);
        if (parsed <= 0 || parsed > 65535)
        {
            std::fprintf(stderr, "usage: %s [port]\n", argv[0]);
            return 1;
        }
        port = static_cast<std::uint16_t>(parsed);
    }

    const std::string addressText = "0.0.0.0:" + std::to_string(port);
    const auto address = X_Y::SocketAddr::Parse(addressText);
    if (!address)
    {
        std::fprintf(stderr, "[server] invalid listen address: %s\n", addressText.c_str());
        return 1;
    }

    try
    {
        X_Y::StreamServer server;
        if (!server.Listen(*address, 128))
        {
            std::fprintf(stderr, "[server] listen on %s failed\n", address->ToString().c_str());
            return 1;
        }

        const auto local = server.GetLocalAddr();
        std::printf("[server] listening on %s\n", local ? local->ToString().c_str() : "?");
        std::printf("[server] waiting for clients... (Ctrl+C to stop)\n");

        unsigned long long clientId = 0;
        for (;;)
        {
            auto connection = server.Accept();
            if (!connection)
                continue; // 阻塞模式下不会返回空；保留以兼容自定义非阻塞 transport

            ++clientId;
            const auto peer = connection->GetPeerAddr();
            std::printf("[server] client #%llu connected from %s\n",
                        clientId, peer ? peer->ToString().c_str() : "?");

            try
            {
                ServeClient(*connection, clientId);
            }
            catch (const std::exception &error)
            {
                std::fprintf(stderr, "[server] client #%llu: %s\n", clientId, error.what());
            }
        }
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "[server] %s\n", error.what());
        return 1;
    }
}
