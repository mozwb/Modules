# Terminal —— 终端能力层（设计稿）

> 版本 0.0.0 · 2026-10-28 · 状态：**设计已确认**；Terminal 输出 / 屏幕、Command、Process、Shell v1 已落地，
> raw mode 还没写
> 位置：`Modules/Terminal` · 依赖：`Core` · 上一层：`Command` / `Shell`

---

## 1. 为什么单独抽一层

现在代码里的三处现状：

- `Log` 自己生成 ANSI：`%r:g:b%` → `\x1b[38;2;r;g;bm`。它不管对面是不是终端。
  输出重定向到文件时会写进脏字节；Windows 上没开 VT 时会显示成 `←[38;2...`。
- `Widget/src/winConfigure.h` 的 `allowConsole()` 已经在做「UTF-8 代码页 + 开 VT」，
  但它是 Widget 的内部头，引擎、构建器、别的工具都够不着，而且只有 Windows 分支。
- `Shell` 只有一份草稿（`clear / setcolor / run / log`），没实现、没进 CMake。

所以先抽 Terminal：**把「这个终端能做什么」和「怎么发控制序列」收在一处**。
Log、Shell、构建器都站在它上面，不再各自手写 `\x1b[`。

层次（你说的 Command 是中间那层，Terminal 里没有它）：

```
        Shell            提示符 / 历史 / 行编辑 / 循环           （最后）
          │
        Command          一条指令：解析 / 内建分派 / 外部进程 / 结果
          │
        Terminal         能力探测 + 样式 + 光标 + 屏幕 + 平台差异
          │
        Core             纯标准库工具
```

- **Terminal 不运行命令。** 它的动词只有 write / clear / move，
  连「命令」这个词都不认识。
- **Command 才是 `run`。** 把一行字解析成命令名 + 参数，查内建表，
  或者交给 Process 起一个外部程序。X_Y 内核那个唯一的动词 `run` 就落在这一层。
- **Shell 只是交互外壳。** 提示符、读一行、历史、补全、循环；
  它把拿到的行交给 Command，把 Command 的输出交给 Terminal。

---

## 2. 边界

**做**

- 探测：stdout / stderr 各自是不是终端、支不支持 ANSI、颜色深度、尺寸、是否 UTF-8
- 初始化：Windows 开 UTF-8 + VT；Linux 基本无事可做
- 输出：UTF-8 字节写入、SGR 样式生成、RGB→256→16 降级、`StripAnsi`
- 屏幕：清屏 / 清行 / 移动光标 / 显隐光标 / 保存恢复 / 标题

**不做**

- 命令解析、内建分派、起外部进程 → `Command`（下一层）
- 提示符、历史、补全、行编辑、循环 → `Shell`（最上层）
- 日志等级、格式、设备 → `Log`
- 窗口 / 控件 / 消息循环 → `Widget`
- `AttachConsole` / `AllocConsole` → 属于 GUI 子系统程序的决定（`Application` / `Widget`）
- 中文、emoji 的显示宽度计算 → 等真要画表格时再说
- 终端模拟器、多路会话、鼠标跟踪、备用屏幕 → 暂时不碰
- 读键 / raw mode → 第一版之后，仍在 Terminal 里，单独文件

依赖只要 `Core`（其实这个模块连 `Core` 都可以不用，但按仓库习惯留着）。
**不依赖 Log**，避免和「Log 以后接 Terminal」形成环。

---

## 3. 四条不变量

1. **不是 tty，就一个控制字节都不发。** 重定向、管道、IDE 输出窗都算。
2. **公开头里不出现 `<windows.h>` / `<termios.h>` / `<unistd.h>`。**
3. **有副作用的函数返回 `bool`，失败不抛异常。** 控制台操作失败是常态：
   重定向、旧终端、服务账户、权限不足。
4. **样式拼装是纯字符串函数。** 脱离终端也能单测，两边平台共用一份。

---

## 4. 平台差异表

| | Windows | Linux / POSIX |
|---|---|---|
| 是不是终端 | `GetConsoleMode(handle, &mode)` 成功 | `isatty(fileno(stream))` |
| 开 ANSI | `SetConsoleMode(mode \| ENABLE_VIRTUAL_TERMINAL_PROCESSING)` | 天生支持，看 `$TERM` |
| UTF-8 | `SetConsoleOutputCP(65001)` | 一般已是 UTF-8，不动 |
| 尺寸 | `GetConsoleScreenBufferInfo` | `ioctl(TIOCGWINSZ)` |
| 颜色深度 | VT 开成功 → TrueColor（老 conhost 只认 16 色，会就近显示，不崩） | `COLORTERM=truecolor/24bit` → TrueColor；`*256color` → 256；否则 16 |
| 无 VT 的清屏 / 移光标 | 返回 false，不写字节 | `$TERM=dumb` 时同样返回 false |

两个补充：

- **按流分别探测。** `app > out.txt` 时 stdout 不是 tty，stderr 仍是终端。
- **`Init()` 只碰控制台模式，不碰进程附着。** 现在 `winConfigure.h` 里的
  `AttachConsole/AllocConsole` 将来留给 Application；它只转调 `Terminal::Init()`。

---

## 5. 文件布局

```
Modules/Terminal/
├── Terminal.h              能力层公开头（include 写 <Terminal/Terminal.h>）
├── Command.h               命令层公开头（include 写 <Terminal/Command.h>）
├── Process.h               外部进程公开头（include 写 <Terminal/Process.h>）
├── Shell.h                 交互壳公开头（include 写 <Terminal/Shell.h>）
├── CMakeLists.txt          平台筛选 + 链接
└── src/
    ├── Terminal.cpp        公共：懒初始化、能力缓存、降级调度
    ├── Ansi.cpp            纯字符串：SGR、RGB→256→16、StripAnsi
    ├── Screen.cpp          清屏 / 清行 / 光标 / 标题（ANSI，非 tty 直接 false）
    ├── Command.cpp         解析 + 注册表 + 分派
    ├── Process.cpp         公共入口：参数校验
    ├── Shell.cpp           提示符 + getline + 内建 + 分发
    ├── Platform.h          Terminal 内部平台接口
    ├── ProcessPlatform.h   Process 内部平台接口
    ├── Win32/TerminalWin32.cpp
    ├── Win32/ProcessWin32.cpp
    ├── Posix/TerminalPosix.cpp
    └── Posix/ProcessPosix.cpp
```

- `Ansi.cpp` 两个平台共用；平台目录只放系统调用。
- 四个公开头分属 `X_Y::Terminal`、`X_Y::Command`、`X_Y::Process`、`X_Y::Shell`，
  共用同一个 `Terminal` 目标，链接一次就够；层还在，只是不再各出一个库。
- `CMakeLists.txt` 用 `xy_module(... EXCLUDE ...)` 排掉对方的平台目录（现成机制）。
- 固定 `STATIC`：控制台模式是进程级状态，跟 exe 走；暂时不做 dll。

```cmake
# Modules/Terminal/CMakeLists.txt
if(WIN32)
    set(_exclude "src/Posix/")
else()
    set(_exclude "src/Win32/")
endif()

xy_module(Terminal STATIC DEPS Core EXCLUDE "${_exclude}")
```

根 `CMakeLists.txt` 的 `XY_MODULE_ORDER` 里放在 `Log` 前面（以后 Log 要靠它判断颜色）：

```cmake
Core Memory Timer Math Terminal Log Files DataStore Decoder Input
```

---

## 6. 公开 API

```cpp
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace X_Y::Terminal
{
    enum class Stream : std::uint8_t { Out, Err };
    enum class ColorDepth : std::uint8_t { None, Ansi16, Ansi256, TrueColor };

    enum class Color : std::uint8_t
    {
        Default,
        Black, Red, Green, Yellow, Blue, Magenta, Cyan, White,
        BrightBlack, BrightRed, BrightGreen, BrightYellow,
        BrightBlue, BrightMagenta, BrightCyan, BrightWhite
    };

    struct Rgb  { std::uint8_t R = 0, G = 0, B = 0; };
    struct Size { std::uint16_t Columns = 0, Rows = 0; };

    struct Caps
    {
        bool       Tty   = false;
        bool       Ansi  = false;
        bool       Utf8  = false;
        ColorDepth Depth = ColorDepth::None;
        Size       Window{};
        std::string TermName;      // "windows-terminal" / "xterm-256color" / ...
    };

    struct Style
    {
        std::optional<Color> Foreground, Background;
        std::optional<Rgb>   ForegroundRgb, BackgroundRgb;
        bool Bold = false, Dim = false, Italic = false,
             Underline = false, Reverse = false, Strike = false;

        Style &SetFg(Color c) { Foreground = c; ForegroundRgb.reset(); return *this; }
        Style &SetFg(Rgb c)   { ForegroundRgb = c; Foreground.reset(); return *this; }
        Style &SetBg(Color c) { Background = c; BackgroundRgb.reset(); return *this; }
        Style &SetBg(Rgb c)   { BackgroundRgb = c; Background.reset(); return *this; }
        Style &SetBold(bool on = true)      { Bold = on; return *this; }
        Style &SetDim(bool on = true)       { Dim = on; return *this; }
        Style &SetItalic(bool on = true)    { Italic = on; return *this; }
        Style &SetUnderline(bool on = true) { Underline = on; return *this; }
        Style &SetReverse(bool on = true)   { Reverse = on; return *this; }
        Style &SetStrike(bool on = true)    { Strike = on; return *this; }
    };

    // ── 探测与生命周期 ──────────────────────────────────────────────
    Caps Probe(Stream stream = Stream::Out);    // 现场查，不改任何状态
    bool Init();                                // 幂等；返回 stdout 的 ANSI 是否可用
    void Shutdown();                            // 还原控制台模式（测试 / 工具用）
    Caps GetCaps(Stream stream = Stream::Out);  // Init 缓存的结果
    bool IsInteractive();                       // stdin + stdout 都是终端

    bool ColorsEnabled(Stream stream = Stream::Out);
    void SetColorsEnabled(bool on);             // 进程级覆盖；NO_COLOR 环境变量优先

    // ── 纯字符串（不碰终端）────────────────────────────────────────
    std::string Sgr(const Style &style, ColorDepth depth);
    std::string Paint(std::string_view text, const Style &style, ColorDepth depth);
    std::string Paint(std::string_view text, const Style &style,
                      Stream stream = Stream::Out);
    std::string StripAnsi(std::string_view text);

    // ── 输出 ────────────────────────────────────────────────────────
    void Write(std::string_view text, Stream stream = Stream::Out);
    void Flush(Stream stream = Stream::Out);

    // ── 屏幕与光标（失败返回 false，且不写任何字节）────────────────
    bool Clear(Stream stream = Stream::Out);                 // 清屏 + 光标归位
    bool ClearLine(Stream stream = Stream::Out);             // 清当前行 + 回车
    bool ClearLines(int count, Stream stream = Stream::Out); // 含当前行，往上 count 行
    bool MoveTo(int row, int column, Stream stream = Stream::Out); // 1-based
    bool MoveUp(int count = 1, Stream stream = Stream::Out);
    bool MoveDown(int count = 1, Stream stream = Stream::Out);
    bool MoveLeft(int count = 1, Stream stream = Stream::Out);
    bool MoveRight(int count = 1, Stream stream = Stream::Out);
    bool SetCursorVisible(bool visible, Stream stream = Stream::Out);
    bool SaveCursor(Stream stream = Stream::Out);
    bool RestoreCursor(Stream stream = Stream::Out);
    bool SetTitle(std::string_view title);

    Size GetSize(Stream stream = Stream::Out);   // 实时查；失败回 Caps.Window
    void Bell(Stream stream = Stream::Out);
}
```

约定：

- `Probe` 现场查、`GetCaps` 读缓存；第一次 `GetCaps/Write` 自动懒初始化，
  `Init()` 可以显式提前调，用来早报错。
- `SetColorsEnabled` 给 `--no-color` 和测试用；`NO_COLOR` 只要存在就禁用颜色。
- `Paint` 只拼字符串、不写；`Write` 才写。两者分开，方便日志设备先拼后发。
- 同时设了枚举色和 RGB 时，**后设的赢**（setter 会清掉另一个）。
- v1 不加锁，输出归主线程；以后要并发再加。

---

## 7. 降级规则

- **非 tty** → `Tty=false, Ansi=false, Depth=None`；`Paint` 原样返回，屏幕函数返回 `false` 不写。
- **tty 但 ANSI 开不了**（老 Windows）→ 样式退化为纯文本；屏幕函数返回 false，不写字节。
- **屏幕 / 光标**：只有 tty + ANSI 才发序列；`SetTitle` 会把标题里的 ASCII 控制字符换成空格。
- **Ansi256**：RGB 先找 6×6×6 色立方，再和灰阶算距离取近者。
- **Ansi16**：RGB 与 16 色调色板算欧氏距离，亮色用 90–97。
- 不主动 strip 调用方文本里已有的转义；要 strip 就显式 `StripAnsi`。

---

## 8. 和 Log / Shell / Widget 的接线（后续，不混在这次）

- **Log**：生成颜色前问 `Terminal::ColorsEnabled()`；文件设备永远不带色。
  现在 `Log.h` 的 `LFile` 会把 `\x1b` 写进文件，这是实打实的 bug，单独一步修。
- **Command / Shell**：解析、内建分派、外部进程在 Command；提示符、历史、行编辑在 Shell。
  `Shell` 还没写，等 Command / Terminal 稳定后再起。
- **Widget**：`winConfigure.h` 删掉 VT / 代码页部分，只留 `AttachConsole`。

---

## 9. 上一层：Command（已落地，和 Terminal 同一个模块）

**它是同一个 `Terminal` 模块里的第二个公开头，但不是能力层的一部分。**
Terminal 只发字节；Command 才认识「命令」。
X_Y 内核只有一个动词 `run`，那个动词就长在这一层。

这一版做到：

- `Parse`：一行字 → `Args{Name, Items}`。空白分隔、单引号原样、
  双引号里转义引号和反斜杠、引号外反斜杠转义、相邻段拼成同一个参数。
- `Registry`：名字 → handler 的 `std::map`；`Add / Remove / Has / Names / Run`。
  查表转小写，同名覆盖，`Names()` 已排序。
- `Result`：`ExitCode / Started / Error / Ok()`，不抛异常。
- 查不到内建命令时默认交给 `Process` 跑外部程序；`SetExternalEnabled(false)` 关掉。
- 退出码约定：0 成功，2 解析失败，127 未知命令 / 启动失败，其他由 handler 或外部程序定。

还没做：

- 管道 `|`、重定向 `>`、`&&`、通配符、环境变量展开（要就 `cmd /c` 或 `sh -c`）。
- 输出：handler 自己 `Terminal::Write`，Command 不碰终端。

```cpp
namespace X_Y::Command
{
    struct Args
    {
        std::string              Name;    // 保留输入大小写
        std::vector<std::string> Items;
    };

    struct Result
    {
        int         ExitCode = 0;
        bool        Started  = true;
        std::string Error;
        bool Ok() const;
    };

    bool Parse(std::string_view line, Args &out, std::string *error = nullptr);

    class Registry
    {
    public:
        using Handler = std::function<Result(const Args &)>;

        bool Add(std::string name, Handler handler);
        bool Remove(std::string_view name);
        bool Has(std::string_view name) const;
        std::vector<std::string> Names() const;   // 小写、已排序

        Result Run(std::string_view line);        // 解析 + 查表 + 执行
        Result Run(const Args &args);

    private:
        std::map<std::string, Handler> _handlers;
    };
}
```

### Process（外部程序，已落地）

```cpp
namespace X_Y::Process
{
    struct Result
    {
        bool        Started  = false;
        int         ExitCode = -1;
        std::string Error;
        bool Ok() const;
    };

    Result Run(std::string_view program, const std::vector<std::string> &arguments);
}
```

- Windows 用 `CreateProcessW`：参数 UTF-8 → UTF-16，命令行按 Windows 规则加引号。
- POSIX 用 `fork + execvp`：CLOEXEC 错误管道，exec 失败能把 errno 带回父进程。
- 继承 stdin / stdout / stderr、环境变量、当前目录；阻塞到子进程退出。
- 不捕获输出、不超时、不解释 shell 语法；要捕获再加 `Capture`。

还开着的口子：

- **输出归谁**：handler 自己通过 `Terminal::Write` 写；外部进程继承句柄。
  以后要捕获输出（测试、日志）再给 Process 加 `Capture` 选项。
- **Process 要不要独立成模块**：现在跟 Terminal 放一起，构建器要用也直接 include；
  真长胖了再拆，公开头不用动。

---

## 10. 最上层：Shell（v1 已落地）

Shell 只负责循环和提示符，不认识具体命令；输入用 `std::getline`。

做到：

- `Shell(Command::Registry &)`、`Run()`、`Execute(line)`、`Stop()`、`SetPrompt()`
- `Terminal::IsInteractive()`（stdin + stdout 都是终端）才打提示符；
  管道喂命令时只有输出，方便测。
- 内建 `help`、`exit [code]`、`quit [code]`；其他交给 Registry。
  `help/exit/quit` 是 Shell 自己的，注册同名命令会被拦截。
- 错误打 `[error] ...`（红色，没颜色就纯文本）；返回最后一条命令的退出码。

```cpp
namespace X_Y::Shell
{
    class Shell
    {
    public:
        explicit Shell(Command::Registry &registry);

        void        SetPrompt(std::string prompt);
        std::string GetPrompt() const;

        int  Execute(std::string_view line);
        int  Run();
        void Stop();
    };
}
```

不做：

- 历史、方向键、Tab 补全、Ctrl+C -> 等 `ReadKey`
- `cd`、管道、重定向 -> 以后

---

## 11. 分步计划（每步停下来验收）

| 步 | 内容 | 验收 |
|---|---|---|
| 1 | 本文档 | 你确认边界和 API |
| 2 | `Terminal.h` + Win32 后端（`Init/Probe/GetCaps/Write/GetSize`）+ demo（已完成） | cmd、Windows Terminal、`> out.txt` 三种场景 |
| 3 | `Ansi` 纯层（`Sgr/Paint/降级/StripAnsi`）+ demo 彩色输出（已完成） | 彩色正常；重定向后文件里没有 `\x1b` |
| 4 | 屏幕操作（清屏 / 清行 / 光标 / 标题）（已完成，Windows 验证；Linux 待验） | 三种终端下不花屏 |
| 5 | POSIX 后端（代码已写，待真 Linux 验证） | 你在服务器上跑一次 |
| 6 | `Command`：Parse + Registry + 纯内建命令（已完成，并入 Terminal 模块） | 不依赖终端也能测 |
| 7 | `Process` + Command 外部命令（已完成，Windows 验证；Linux 待验） | `cmd /c dir` / `ls` 能跑，退出码对 |
| 8 | `Shell` v1：提示符 / 内建 / 循环（已完成；历史、行编辑未做） | 管道喂命令能跑，退出码对 |
| 9 | `Terminal` raw mode + `ReadKey` + 行编辑 / 历史 | Shell 能方向键、退格 |
| 10 | 接 Log / Widget | 单独一步 |

---

## 12. 已定 / 还缺

已定（2026-10-28）：

1. **三层**：Terminal 底层、Command 中间、Shell 最上层。
2. **第一版 Terminal 只做输出**：探测 + 样式 + 屏幕，键盘输入以后再说。
3. **没有 VT 的老 Windows**：颜色退成纯文本，不做 `SetConsoleTextAttribute` 16 色兜底。
4. **Linux 验证**：幼仙在服务器上跑；本机 WSL 被拒、没有 Docker、MinGW 也交叉编不了。

还缺：

- Process 暂时放在 Terminal 模块里，构建器要用直接 include；真要独立再拆。
- Shell 的行编辑 / 历史还没做，现在靠终端自己的 `getline`。
