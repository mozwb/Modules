// ─────────────────────────────────────────────────────────────────────────────
// tcp_echo_client.cpp — 基于 X_Y Network 模块的最小 TCP 回显客户端
//
//   X_Y::StreamClient      连接 / 收发 / 查询本端和对端地址
//   X_Y::Buffer            发送与接收缓冲
//
// 用法：
//   echo_client [host] [port] [--auto]
//     host    默认 127.0.0.1
//     port    默认 9000
//     --auto  自动发 3 条测试消息后退出（适合快速验证连通性）
//
// 构建：见同目录 CMakeLists.txt（会一起编译 Network + Buffer/Memory 源码）
// ─────────────────────────────────────────────────────────────────────────────

#include "Network/Endpoints.h"
#include "Network/NetworkError.h"
#include "XCore/Memory/Buffer.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    constexpr std::uint64_t kReadCapacity = 4096;
    constexpr std::size_t kMaxPendingBytes = 1024 * 1024;

    // 从字节流里读出一整行（'\n' 之前的部分）。pending 保存上次没读完的尾巴。
    bool ReceiveLine(X_Y::StreamClient &client, X_Y::Buffer &chunk,
                     std::string &pending, std::string &line)
    {
        for (;;)
        {
            const std::size_t newline = pending.find('\n');
            if (newline != std::string::npos)
            {
                line = pending.substr(0, newline);
                pending.erase(0, newline + 1);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                return true;
            }

            const std::uint64_t received = client.ReceiveSome(chunk);
            if (received == 0)
                return false; // 服务端关闭

            pending.append(reinterpret_cast<const char *>(chunk.Data),
                           static_cast<std::size_t>(chunk.Size));
            if (pending.size() > kMaxPendingBytes)
                throw std::runtime_error("response line is too long");
        }
    }

    // 一条消息 = 一行。发送后等待一条回显。
    bool Exchange(X_Y::StreamClient &client, X_Y::Buffer &chunk,
                  std::string &pending, const std::string &message)
    {
        std::printf("[client] -> %s\n", message.c_str());

        X_Y::Buffer wire;
        wire.Append(message.data(), message.size());
        wire.Append("\n", 1);
        client.SendAll(wire);

        std::string reply;
        if (!ReceiveLine(client, chunk, pending, reply))
        {
            std::printf("[client] connection closed by server\n");
            return false;
        }

        std::printf("[client] <- %s\n", reply.c_str());
        return true;
    }
}

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    const bool autoMode = argc > 3 && std::strcmp(argv[3], "--auto") == 0;

    std::uint16_t port = 9000;
    if (argc > 2)
    {
        const int parsed = std::atoi(argv[2]);
        if (parsed <= 0 || parsed > 65535)
        {
            std::fprintf(stderr, "usage: %s [host] [port] [--auto]\n", argv[0]);
            return 1;
        }
        port = static_cast<std::uint16_t>(parsed);
    }

    try
    {
        X_Y::StreamClient client;
        client.Connect(host, port);

        const auto local = client.GetLocalAddr();
        const auto peer = client.GetPeerAddr();
        std::printf("[client] connected: local %s -> peer %s\n",
                    local ? local->ToString().c_str() : "?",
                    peer ? peer->ToString().c_str() : "?");

        X_Y::Buffer chunk(kReadCapacity);
        if (!chunk)
            throw std::runtime_error("failed to allocate receive buffer");

        std::string pending;
        bool ok = true;

        if (autoMode)
        {
            for (int i = 1; i <= 3 && ok; ++i)
                ok = Exchange(client, chunk, pending, "ping " + std::to_string(i));
        }
        else
        {
            std::printf("[client] type a message and press Enter (/quit to exit)\n");
            std::string line;
            while (std::getline(std::cin, line))
            {
                if (line == "/quit")
                    break;
                if (!Exchange(client, chunk, pending, line))
                {
                    ok = false;
                    break;
                }
            }
        }

        client.Close();
        return ok ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "[client] %s\n", error.what());
        return 1;
    }
}
