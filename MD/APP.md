# APP

`APP` 是应用层的内核，不是窗口层。它只收两件事：

1. **Loop**：一次循环推进（平台消息、命令读取、或以后 X_Y 的帧循环）
2. **EventBus**：事件的入队、分层消费、按 sender/type 广播

`Application` 把这两部分拼起来，并负责运行标志与延迟回收安全点。
窗口、DPI、窗口类注册、主窗口退出语义都不属于这里。

---

## 1. 目录

```
Modules/APP/
  Application.h        Application：Loop + EventBus + 运行状态 + 延迟回收
  EventBus.h           XMovement / EventDispatcher / EventQueue / Layer / LayerStack
  Loop.h               Loop 接口 + CreatePlatformLoop()
  Library.h            动态库 RAII：Open / Symbol / Close
  MicroProgram.h       DLL 微程序：加载 + 调 xy_program_v1
  src/
    Application.cpp
    Library.cpp
    MicroProgram.cpp
    Win32/LoopWin32.cpp
    Win32/LibraryWin32.cpp
    Posix/LoopPosix.cpp
    Posix/LibraryPosix.cpp
```

依赖：`Core`。不依赖 Widget、Movement、Terminal、Log。

---

## 2. Loop

```cpp
class Loop
{
public:
    virtual ~Loop() = default;
    virtual void Boot() {}          // 只做平台启动，不做窗口业务
    virtual bool Pump() = 0;        // 推进一次；false = 收到退出
    virtual void Stop() {}          // 请求退出（Pump 可以不实现，Application 会先看 Running）
};
```

- Windows 默认实现只做 `PeekMessage / Translate / DispatchMessage`。
- POSIX 默认实现先留空转（1ms），以后 X_Y 的命令循环可以注入自己的 `Loop`。
- `Application` 支持注入 `std::unique_ptr<Loop>`，这样以后 X_Y 可以直接用
  “读一行命令”的循环，不必被 Win32 消息循环绑死。

窗口类注册仍由 Widget 的 `Entry.cpp` / `Win32Class` 负责；APP 不碰。

---

## 3. EventBus

从原 `Movement/Movements.h` 抽出来的**广播机制**，不是事件类型：

- `XMovement`：事件基类（sender / Handled / DispatchEvent）
- `EventDispatcher`：绑定表、Connect / disConnect / Dispatch
- `EventQueue`：待派发事件队列
- `Layer` / `LayerStack`：派发前的高优先级消费者
- `EventBus`：上面三者的组合，`Application` 持有它

具体事件已经收敛到 Widget 层：`Widget/Movement/` 保留
`MovementType`、`Movement`、`KeyPressed` 等类型，通过 `using` 复用 APP 的
EventBus 类型。APP 只提供机制，不依赖这批具体事件。

派发顺序保持原样：

```
EventBus.Queue
  → LayerStack（先消费）
  → 可选 pre-dispatch hook
  → EventDispatcher（Connect 的绑定）
  → delete event
  → FlushDeferredRecycle（安全点）
```

---

## 4. Application

```cpp
class Application
{
public:
    explicit Application(std::unique_ptr<Loop> loop = nullptr);
    void Run();                       // 循环 Pump + ProcessEvents
    void Stop();
    void Push(XMovement* e);
    void ProcessEvents();

    EventBus& GetEventBus();
    EventDispatcher& GetDispatcher();
    EventQueue& GetEventQueue();

    void DeferRecycle(std::function<void()> action);
    void FlushDeferredRecycle();
};
```

和旧版相比：

- 去掉主窗口 / 托管窗口 / SetMainWindow / Shutdown。这些是 Widget 的职责，
  已经移到 `Widget/WindowHost`。
- `ProcessEvents` 一轮末尾仍然 flush 延迟回收，窗口自毁的 UAF 保护不变。
- 保留 `exec / isRunning / appClose / pushEvents` 这类旧名字做薄别名，
  迁移期不用一次改完所有调用点。
- `Connect / connect / disConnect` 便利函数放在 APP，只操作
  `App::Application` 的 dispatcher，不认识 Widget。

---

## 5. 微程序加载（X_Y 预留）

X_Y 的缺口不是命令解析，也不是外部进程：
`Terminal/Command` 和 `Terminal/Process` 已经够用。缺的是：

> 把一个 DLL 当微程序载入，拿到 `xy_program_v1`，把 host 表和 argv 交过去。

APP 只提供最小两块：

```cpp
App::Library        // LoadLibraryA / dlopen 的 RAII
App::MicroProgram   // Library + 入口符号 + Run(host, argc, argv)
```

`MicroProgram` 不包含 `xy_api.h`，入口名用字符串传，默认 `"xy_program_v1"`；
以后 X_Y 的 `ModuleRegistry` 也可以复用 `Library` 加载 `xy_module_v1`。

命令装载 / 外部程序挂起仍由 Terminal 层负责；它们在 handler 里阻塞执行，
不需要 APP 再包一层线程或超时。

---

## 6. Widget 接线与依赖方向

单向依赖：

```
Widget/XWidget
Widget/Movement
Widget/EventHelpers
Widget/WindowHost        →  APP/Application (Loop + EventBus)  →  Core
Widget/Win32WndProc
UI/Container
```

`Widget/Movement` 是具体事件类型，`Widget/EventHelpers` 是 `Connect / connect /
disConnect` 便利函数；两者都属于 Widget 层，不属于 APP。UI 只 include
`Widget/...`，不直接 include APP。

- `Widget/WindowHost` 是 Widget 侧单例，承接主窗口 / 托管窗口 / Shutdown。
  它认识 `XWidget` 和 `App::Application`；APP 不知道它。
- `XWidget` 自毁时只向 `App::Application::DeferRecycle` 登记回收，
  向 `WindowHost::ForgetOwnedWindow` 摘除名单，两边都不反向 include Widget。
- `Win32WndProc` 把 Win32 消息转成 Movement 后投进
  `App::Application::GetEventQueue()`。
- APP 的公开头里不允许出现 `Widget/`、`Widget/Movement/`、`UI/` include；
  这条靠 grep 检查。

已清理：

- `Widget/Application.h`
- `Widget/src/Application.cpp`
- `Widget/PlatformLoop.h`
- `Widget/src/Win32/PlatformLoopWin32.cpp`
- `Modules/Movement/` 整个模块（内容搬到 `Widget/Movement/`）

Windows 平台默认 Loop 现在由 `APP/src/Win32/LoopWin32.cpp` 提供；
窗口类注册 / DPI 仍由 Widget 的 `Entry.cpp` 负责，APP 不碰。

验收状态：

- APP-only 构建通过，`AppDemo` 0 失败。
- `GraphicsContext` 已从 `UnSureEnable` 恢复到独立模块，Windows 下
  `xy_vendor_glad + GraphicsContext` 构建通过。
- Widget / Media / UI 已接进 Modules 的 CMake；Windows 全链路构建通过。
- `Programs/UiDockDemo` 实际启动过：TopLayout 五块 Dock、LogViewer、
  HexViewer、MediaPlayerPanel、ImageViewer 和 tab 切换都能显示。
- 入口不再只依赖 `Entry.cpp`：`BaseWin` 会自己完成 DPI-aware 和
  Win32 窗口类注册，控制台程序直接建窗也能跑。
