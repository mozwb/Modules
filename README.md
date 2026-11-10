# Modules

我的代码库。

---
## 项目结构

```
Modules
├── Core/                  # 🔩 核心基础设施（独立 lib）
│   ├── FilesSystem/       #   文件读写 + Win32 对话框
│   ├── Input/             #   键盘/鼠标按键映射
│   ├── Log/               # 🟢 自研彩色日志系统（最成熟）+ DataStoreDevice 后端
│   ├── Memory/            #   Buffer 内存池 + RingBuffer + LoopQueue 环形队列
│   ├── Thread/            #   C++20 线程、互斥锁、条件变量
│   ├── Timer/             #   计时 / 性能分析（含 Ticker 周期线程定时器）
│   ├── XCore/             #   通用核心、文件/内存和 image/audio/video 解码
│   └── XMath/             #   数学库
│
│
├── DataStore/         # 🅱️ 全局数据系统（以 OS 文件系统为库，Buffer=存储格式）
├── Image/             #   图片解析
├── Middle/            #   中间数据结构（MeshData）
│
│
├── GraphicsContext/   #   OpenGL 上下文封装（从 UnSureEnable 恢复的独立模块）
├── APP/               #   应用层内核：Loop + EventBus + Application + DLL 微程序
├── Widget/                   # 🏢 上层应用基础设施（独立 lib）
│   ├── Movement/          #   具体事件类型（窗口 / 键盘 / 鼠标）
│   ├── EventHelpers.h     #   Connect / connect / disConnect 便利函数
│   ├── WindowHost.h       #   窗口生命周期（主窗口 / 托管 / 退出清理）
│   └── Widget/            #   窗口系统（Win32 封装 + XWidget）+ Canvas/Font/DPI 平台抽象
├── UI/                #   自研原生 UI（Container/Component 两层）+ 原生组件
│
│
├── Render/                # 🎨 OpenGL 渲染抽象层
│   └── Render/            #   VAO/VBO/IBO/Shader/Texture/Camera/
├── Model/             #   OBJ 模型加载 + 立方体生成 + GPU 转换
│                           #   FrameBuffer/UniformBuffer + OpenGL 实现
├── Physical/                    # 轻量物理引擎
│
├── MD/                    # 📖 模块文档（Log、Window 等）
├── vendor/                #   第三方库（自带 CMake 自动接入，接线脚本在 vendor/cmake/）

```
---

## Thread

`Thread` 基于 C++20 `std::jthread`。线程任务可以接收 `std::stop_token` 并主动检查停止请求；
销毁线程对象会请求停止并等待线程结束，因此任务应及时响应停止请求。线程对象不可复制，
需要时可以移动。

```cpp
#include <Thread/Thread.h>
#include <Thread/Mutex.h>
#include <Thread/ConditionVariable.h>

using namespace X_Y;

Mutex mutex;
ConditionVariable condition;
bool ready = false;

Thread worker;
worker.Start([&](std::stop_token)
{
    UniqueLock lock(mutex);
    condition.Wait(lock, [&] { return ready; });
});

{
    LockGuard lock(mutex);
    ready = true;
}
condition.NotifyOne();
worker.Join();
```

`LockGuard` 用于短作用域自动加锁/解锁；条件变量等待使用 `UniqueLock`，并且必须提供谓词，
以正确处理虚假唤醒。`WaitFor` 可用于带超时的谓词等待。

### ThreadPool

线程池使用固定数量的工作线程和 FIFO 队列。`Submit` 接受可调用对象及其参数，返回 `std::future`；
通过 `get()` 取得返回值，或重新抛出任务中的异常。构造时工作线程数为 `0` 会使用硬件并发数，
并保证至少创建一个工作线程。

```cpp
#include <Thread/ThreadPool.h>

X_Y::ThreadPool pool(4);
auto result = pool.Submit([](int a, int b) { return a + b; }, 20, 22);
int value = result.get();
pool.Shutdown();
```

`Shutdown()` 会拒绝后续任务，执行完已排队的任务后等待所有工作线程退出；析构时也会自动关闭。
不要从该线程池自己的任务中调用 `Shutdown()`，也不要在池内任务中等待一个必须由同一线程池执行的
后续任务，否则可能发生死锁。

## Network

`Network` 提供 IPv4/IPv6 endpoint 解析、基于 TCP 的流连接/监听，以及基于 UDP 的数据报收发。
TCP 是字节流，`SendSome` / `ReceiveSome` 不保证一次处理完整消息；需要完整发送时可使用 `SendAll`，
而消息边界和协议帧由调用方定义。UDP 每次 `SendTo` / `ReceiveFrom` 对应一个数据报，
接收结果会报告发送方地址和是否发生截断。

```cpp
#include <Network/Endpoints.h>
#include <chrono>
#include <stdexcept>

X_Y::StreamClient client;
if (!client.SetTimeout(
        X_Y::SocketTimeoutDirection::Receive,
        std::chrono::seconds(5)))
    throw std::runtime_error("could not configure receive timeout");
client.Connect("example.com", 80);
client.SendAll("hello", 5);
```

传输 API 是同步阻塞式的。Windows 和 Linux 都实现了 `SetTimeout(direction, duration)`，
可为 socket 的发送或接收设置超时；
零时长表示禁用超时；超时作用于每次阻塞的 socket 收发调用，不是 `SendAll` 等整段操作的总时限，
操作超时会抛出 `NetworkTimeoutError`。客户端可在连接前设置收发超时，
服务端设置的超时会应用到之后 `Accept()` 得到的连接。此选项不限制 DNS 解析、连接建立或
`Accept()` 等待；取消阻塞操作目前尚未实现。Network 在构建时按平台选择 Windows 或 Linux
socket 实现。对已创建的 socket，`SetTimeout` 返回 `false` 表示系统未能应用选项；注入的自定义
transport 也可以返回 `false` 表示不支持。连接前配置的选项会在 `Connect()` 创建 socket 时应用，
若应用失败，`Connect()` 会抛出 `NetworkError`。
`SendAll()` 由多次 socket 发送组成；如果中途超时，可能已有部分字节发出，因此不能盲目从头重试整条消息。
`ctest --test-dir build` 在 Windows 和 Linux 运行 loopback TCP/UDP、Endpoint/Reactor、
非阻塞操作及发送队列测试。

### Reactor（开发中）

`IoContext` 是 Reactor 事件循环，Windows 使用 `WSAPoll`，Linux 使用 `epoll` 等待 socket 可读/可写事件。
可用 `Watch()` 注册 `SocketHandle`，它会将 socket 切为非阻塞并返回 RAII `Registration`；
销毁或 `Reset()` 会解除注册，
`Update()` 可修改关注方向。回调在调用 `Run()` 的线程执行；一个 `IoContext` 同时只允许一个
`Run()` 调用。`Stop()` 可从其他线程请求停止，并唤醒事件循环。

`ScheduleAfter(delay, handler)` 可注册一次性定时器；定时器句柄析构或调用 `Cancel()` 会取消尚未触发的定时器。
定时器回调在 `Run()` 所在线程执行，可从回调中调用 `Stop()`。例如，可在非阻塞连接等待期间安排超时，
并在超时回调中关闭仍处于连接中的 `StreamClient`；关闭 socket 会结束该连接尝试。该定时器不提供周期执行，
也不会自动关闭连接。

`Post(task)` 可从其他线程安全地把任务投递给事件循环；任务在 `Run()` 线程执行。空任务会抛出
`std::invalid_argument`，已经停止的 `IoContext` 返回 `false`，停止后尚未执行的任务不会继续运行。

`ResolveAsync(host, port, handler)` 在线程池中执行系统名称解析，并把
`handler(addresses, error)` 投递回 `Run()` 线程；解析失败时 `error` 非空，可用
`std::rethrow_exception(error)` 获取 `NetworkError`。返回的 `ResolveRequest` 必须保留到请求完成；
调用 `Cancel()` 或销毁句柄会抑制尚未投递的回调。操作系统的 `getaddrinfo` 本身不可取消，因此取消只保证
不再调用用户回调，不会中断正在执行的系统解析。解析工作由最多两个 worker 共享执行。

例如，异步连接可组合解析、非阻塞连接和定时器：先保存 `ResolveRequest`，在解析回调中调用
`ConnectNonBlocking(address)`；若返回 `InProgress`，注册写就绪并调用 `FinishConnect()`。同时用
`ScheduleAfter()` 安排超时，在超时回调关闭客户端并停止/推进应用状态；连接成功时取消定时器。
这几个步骤都在 `IoContext` 回调线程串行执行，避免跨线程操作同一个 Endpoint。

`SocketHandle` 现提供 `TryAccept`、`TrySendSome`、`TryReceiveSome`、`TrySendTo` 和
`TryReceiveFrom`。这些操作必须先对 socket 显式调用 `SetNonBlocking(true)`；就绪回调中可按
`SocketIoStatus` 处理 `Completed`、`WouldBlock` 和流连接关闭时的 `Closed`。`Completed` 时
结果的 `value` 才有值；TCP `TrySendSome` 仍可能只发送部分数据，调用方需自行排队剩余字节。
`TryAccept` 返回的新连接会自动设为非阻塞。

TCP/UDP Endpoints 也提供对应的 `Try*` 操作，以及 `Watch(IoContext&, interest, handler)`。
`Watch()` 自动把底层 socket 设为非阻塞，并由 Endpoint 持有注册；Endpoint 关闭或析构时会先注销。
自定义 transport 若未实现非阻塞操作或 Reactor 注册，会明确抛出 `std::logic_error`。启用
`Try*` 或 `Watch()` 后，不要再对同一 Endpoint 使用同步阻塞收发；解除监听不会恢复阻塞模式。
`Connect(host, port)` 仍同步执行 DNS 解析和连接建立。WSAPoll 会扫描注册 socket，后续需通过压力
测试评估其在大量连接下的表现；Linux 使用 `epoll` 后端。`StreamClient::ConnectNonBlocking(address)`
已支持基于数值 IP 地址启动非阻塞连接；若返回 `InProgress`，用 `Watch(..., Write, ...)`
监听连接 socket，在可写/错误通知中调用 `FinishConnect()` 检查结果。连接失败会抛出 `NetworkError`。
`Connect(host, port)` 仍同步解析 DNS；需要异步解析时使用 `IoContext::ResolveAsync()`，再组合非阻塞连接。

`StreamSendQueue(capacityBytes)` 提供有界、FIFO 的 TCP 待发送队列。`Enqueue()` 要么完整加入数据，
要么在容量不足时返回 `false`，不会部分接受。`Flush()` 处理部分写入和 `WouldBlock`，并返回本轮
发送字节数及仍待发送的字节数；每次调用有字节预算，避免单个连接长时间占用事件循环。队列不会自动
注册可写事件：有待发送数据时调用 `UpdateInterest(ReadWrite)`，队列清空后切回 `Read`，以免可写事件
造成空转。队列不是线程安全的，应和对应 Endpoint 一样限制在事件循环线程使用。队列满时由应用决定
暂停上游、稍后重试或拒绝数据。

```cpp
X_Y::StreamSendQueue outgoing(256 * 1024); // 最多缓存 256 KiB

// 已经在 io 回调中，并且 connection 已通过 Watch 注册
if (!outgoing.Enqueue(message.data(), message.size()))
{
    // 队列满：由应用做背压处理，不要丢掉已接受的数据
    return;
}
connection.UpdateInterest(X_Y::IoInterest::ReadWrite);

// 收到可写通知时
const auto flushed = outgoing.Flush(connection);
if (flushed.status == X_Y::SocketIoStatus::Closed)
    connection.Close();
else if (outgoing.Empty())
    connection.UpdateInterest(X_Y::IoInterest::Read);
```

### Log 异步输出

Log 默认仍然同步输出。已有 logger 可通过 `EnableAsync()` 切换到异步模式；格式化仍在调用线程完成，
Log 将独立日志记录放入 FIFO 队列，由专用消费者线程按提交顺序取出并写入设备。异步模式下可用
`Flush()` 等待此前日志写完；`DisableAsync()` 会排空队列并切回同步输出。异步设备抛出的异常会在
`Flush()` 或 `DisableAsync()` 时重新抛出。

```cpp
logger.EnableAsync();
XINFO("后台写入日志: {}", value);
logger.Flush();
logger.DisableAsync();
```

专用消费者线程不占用通用线程池的工作线程。队列当前不设上限。异步日志器会持有每条记录所需设备的
共享引用，因此排队期间调用 `clear()` 或 `remove()` 不会提前销毁仍在使用的设备。程序退出前建议显式
`Flush()`，以便等待输出并处理设备错误。
