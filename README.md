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
可为 socket 的发送或接收设置超时；当前阶段只在 Windows 上构建和测试，Linux 测试之后补；
零时长表示禁用超时；超时作用于每次阻塞的 socket 收发调用，不是 `SendAll` 等整段操作的总时限，
操作超时会抛出 `NetworkTimeoutError`。客户端可在连接前设置收发超时，
服务端设置的超时会应用到之后 `Accept()` 得到的连接。此选项不限制 DNS 解析、连接建立或
`Accept()` 等待；取消阻塞操作目前尚未实现。Network 在构建时按平台选择 Windows 或 Linux
socket 实现。对已创建的 socket，`SetTimeout` 返回 `false` 表示系统未能应用选项；注入的自定义
transport 也可以返回 `false` 表示不支持。连接前配置的选项会在 `Connect()` 创建 socket 时应用，
若应用失败，`Connect()` 会抛出 `NetworkError`。
`SendAll()` 由多次 socket 发送组成；如果中途超时，可能已有部分字节发出，因此不能盲目从头重试整条消息。
`ctest --test-dir build` 在 Windows 运行 loopback TCP/UDP、endpoint 解析及 TCP/UDP 接收超时测试。
Linux 有对应实现，但当前暂不构建或运行这组测试，后续再验证。

### Windows Reactor（开发中）

`IoContext` 是 Windows Reactor 的事件循环基础，当前通过 `WSAPoll` 等待 socket 可读/可写事件。
可用 `Watch()` 注册 `SocketHandle`，它会将 socket 切为非阻塞并返回 RAII `Registration`；
销毁或 `Reset()` 会解除注册，
`Update()` 可修改关注方向。回调在调用 `Run()` 的线程执行；一个 `IoContext` 同时只允许一个
`Run()` 调用。`Stop()` 可从其他线程请求停止，并唤醒事件循环。

`SocketHandle` 现提供 `TryAccept`、`TrySendSome`、`TryReceiveSome`、`TrySendTo` 和
`TryReceiveFrom`。这些操作必须先对 socket 显式调用 `SetNonBlocking(true)`；就绪回调中可按
`SocketIoStatus` 处理 `Completed`、`WouldBlock` 和流连接关闭时的 `Closed`。`Completed` 时
结果的 `value` 才有值；TCP `TrySendSome` 仍可能只发送部分数据，调用方需自行排队剩余字节。
`TryAccept` 返回的新连接会自动设为非阻塞。

TCP/UDP Endpoints 也提供对应的 `Try*` 操作，以及 `Watch(IoContext&, interest, handler)`。
`Watch()` 自动把底层 socket 设为非阻塞，并由 Endpoint 持有注册；Endpoint 关闭或析构时会先注销。
自定义 transport 若未实现非阻塞操作或 Reactor 注册，会明确抛出 `std::logic_error`。启用
`Try*` 或 `Watch()` 后，不要再对同一 Endpoint 使用同步阻塞收发；解除监听不会恢复阻塞模式。
非阻塞连接建立还未实现，`Connect()` 仍是同步操作。WSAPoll 会扫描注册 socket，先用于验证和学习，
不保证适合极大量连接；后续需通过压力测试评估。Linux 的 Reactor 后端尚未实现；目前 Linux 构建
提供显式的 unsupported backend，创建 `IoContext` 会抛出 `std::runtime_error`。

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
