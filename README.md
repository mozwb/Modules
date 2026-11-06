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
