# TCP Echo 示例（基于 X_Y Network 模块）

两个控制台程序，用来验证两台机器之间的 TCP 连通性，同时演示仓库已有的网络抽象：

| 模块类型 | 用途 |
|---|---|
| `X_Y::SocketAddr` | 解析 / 格式化 `"0.0.0.0:9000"`、`"127.0.0.1:9000"` |
| `X_Y::StreamServer` | 监听、`Accept()` |
| `X_Y::StreamClient` | `Connect()`、查询本端/对端地址 |
| `X_Y::StreamConnection` | `SendAll()` / `ReceiveSome()` |
| `X_Y::Buffer` | 收发缓冲 |

| 文件 | 作用 |
|---|---|
| `tcp_echo_server.cpp` | 监听端口、接受连接、逐行回显 |
| `tcp_echo_client.cpp` | 连接服务端，交互式发消息或 `--auto` 自动测试 |
| `CMakeLists.txt` | 独立构建（只编译 Network + Buffer 相关源码，不依赖根工程） |

## 协议

- TCP，一行一条消息，以 `\n` 结尾（客户端发 `\r\n` 也兼容）。
- 服务端回复 `echo: <原消息>\n`。
- 输入 `/quit` 客户端正常断开。

## 构建

根目录的 `CMakeLists.txt` 要求 MinGW + vendored FFmpeg，不适合在 Codespace 里跑；
这个示例目录有自己的 `CMakeLists.txt`，只取真正用到的源码：

```bash
cmake -S . -B build
cmake --build build
```

它会编译：

- `Modules/Network/src/Endpoints.cpp`
- `Modules/Network/src/SocketAddr.cpp`
- `Modules/Network/src/NetworkError.cpp`
- `Modules/Network/src/Linux/SocketHandleLinux.cpp`（Windows 则是 `Windows/SocketHandleWindows.cpp`）
- `Modules/XCore/src/Memory/Buffer.cpp` + `XMemFacade.cpp` + `XMemBackend.cpp` + `XMemStats.cpp`

> 为什么带上 Memory 的四个文件：`Endpoints` 的接口使用 `X_Y::Buffer`，
> 而 `Buffer.cpp` 的 `Reserve`/`Release` 走 `Memory::Instance().Alloc/Free`。
> 头文件本身不依赖 Memory，链接时才需要这四个实现文件（`XMemGlobalNew.cpp` 可选）。

也可以手写 g++（在当前目录内）：

```bash
g++ -std=c++20 -O2 -Wall -Wextra -I ../Modules \
    tcp_echo_server.cpp \
    ../Modules/Network/src/Endpoints.cpp \
    ../Modules/Network/src/SocketAddr.cpp \
    ../Modules/Network/src/NetworkError.cpp \
    ../Modules/Network/src/Linux/SocketHandleLinux.cpp \
    ../Modules/XCore/src/Memory/Buffer.cpp \
    ../Modules/XCore/src/Memory/XMemFacade.cpp \
    ../Modules/XCore/src/Memory/XMemBackend.cpp \
    ../Modules/XCore/src/Memory/XMemStats.cpp \
    -o echo_server
```

Windows 把 `Linux/SocketHandleLinux.cpp` 换成 `Windows/SocketHandleWindows.cpp`，并加 `-lws2_32`。

## 运行

服务端（Codespace 里）：

```bash
./echo_server 9000
# [server] listening on 0.0.0.0:9000
```

先在 Codespace 内自测：

```bash
./echo_server 9000                    # 终端 1
./echo_client 127.0.0.1 9000 --auto   # 终端 2
```

预期输出：

```text
[client] connected: local 127.0.0.1:50632 -> peer 127.0.0.1:9000
[client] -> ping 1
[client] <- echo: ping 1
...
```

## 从本机连 Codespace

Codespace 的端口不是直接暴露的，选一种方式把远端 9000 映射到本机：

1. **VS Code Ports 面板（最简单）**：连上 Codespace 后，`PORTS` 面板会列出 9000；也可以手动 Forward。随后本机直接连 `127.0.0.1:9000`。
2. **GitHub CLI**：
   ```bash
   gh codespace ports forward 9000:9000 -c <codespace-name>
   ```
   本机仍然连 `127.0.0.1:9000`。
3. **SSH 本地转发**：
   ```bash
   ssh -L 9000:localhost:9000 <codespace-ssh-host>
   ```

本机运行客户端：

```powershell
.\echo_client.exe 127.0.0.1 9000 --auto
.\echo_client.exe 127.0.0.1 9000          # 交互模式
```

> 注意：`https://<名字>-9000.app.github.dev` 是 HTTP(S) 代理地址，普通 TCP 客户端不能直接连它。
> 要么用上面的端口转发，要么给客户端加 TLS 走 443。

## 代码里值得注意的点

- 地址解析用 `SocketAddr::Parse("0.0.0.0:9000")`（只接受字面 IP；域名交给 `StreamClient::Connect(host, port)` 内部的 `getaddrinfo`）。
- `StreamServer::Listen` 内部已经设置了 `SO_REUSEADDR`，服务端重启不用等 `TIME_WAIT`。
- `StreamConnection::SendAll` / `StreamClient::SendAll` 会循环处理短写，不需要自己写 `send` 循环。
- `ReceiveSome(Buffer&)` 把本次读到的字节数写进 `Buffer.Size`；返回 `0` 表示对端关闭，失败会抛 `NetworkError`。
- TCP 仍是字节流：示例用 `std::string pending` 按 `\n` 切分，处理“一次读到半行/多行”的情况。
- 服务端单线程、一次服务一个客户端。并发可以下一步改成 `std::thread` 或 `poll/epoll`。
- 平台差异被 `SocketHandleLinux.cpp` / `SocketHandleWindows.cpp` 吸收，示例代码不需要 `#ifdef _WIN32`。

## 关于 `Buffer` 与 `Memory` 的依赖

结论：**`Buffer.h` 不依赖 Memory；`Buffer.cpp` 依赖分配门面，但不需要“整个 Memory 模块”。**

- [`Buffer.h`](../Modules/XCore/Memory/Buffer.h) 只包含标准库头，模板方法都在头文件里。
- [`Buffer.cpp`](../Modules/XCore/src/Memory/Buffer.cpp) 的 `Reserve` / `Release` 调用
  `Memory::Instance().Alloc(...)` / `Free(...)`（声明在 `XMemFacade.h`）。
- 最小链接集合就是本示例 CMake 里列的四个文件：
  `Buffer.cpp`、`XMemFacade.cpp`、`XMemBackend.cpp`、`XMemStats.cpp`。
  `XMemGlobalNew.cpp` 只在需要拦截全局 `new/delete` 时才要。
- 这些文件不依赖 FFmpeg / UI / 文件系统。`XCore` 这个 CMake target 链接 FFmpeg 是 target 级别的依赖，
  不是 `Buffer` 本身的依赖。
- 想彻底解耦，只需改 `Buffer.cpp` 里那两处分配/释放（或引入分配器策略），`Buffer.h` 不用动。
- 写这个示例时顺带发现并修复了 `Buffer::Reserve` 的一个已有 bug：扩容路径调用 `Release()` 会把 `Size` 清零，
  导致 `Append` 续写时覆盖已有内容（回显前缀 `echo: ` 就是这样丢的）。现在扩容后会恢复 `Size`。
