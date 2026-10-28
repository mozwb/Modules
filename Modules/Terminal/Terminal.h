#pragma once

/*
Terminal.h
version 0.0.0

终端能力层。只回答两件事：
  . 这个进程面前的终端能做什么（是不是 tty / ANSI / 颜色深度 / 尺寸）
  . 怎么把样式和文字变成它认识的字节

不负责：命令解析（Command）、日志格式（Log）、窗口（Widget）、键盘输入（下一版）。

────────────────────────────────────────────────────────────────
不变量

  1. 不是 tty，就不发控制字节。重定向、管道、IDE 输出窗都安全。
  2. 公开头里没有 <windows.h> / <termios.h>，平台代码只在本模块 src/ 里。
  3. 会失败的操作（探测、屏幕、尺寸）返回 bool 或空值，不抛异常。
  4. Sgr / Paint / StripAnsi 是纯函数，脱离终端也能单测。

平台

  Windows   Init() 打开 ENABLE_VIRTUAL_TERMINAL_PROCESSING 和 UTF-8 代码页；
            没有 VT 的老控制台，颜色会自动退成纯文本。
  Linux     不用开什么，看 $TERM / $COLORTERM 判断能力。

颜色

  Paint() 只拼字符串，不写；Write() 才写。
  非 tty 时颜色自动关闭；另外可用 SetColorsEnabled(false) 或环境变量
  NO_COLOR 手动关闭。真彩色会按终端能力自动降到 256 / 16 / 无。

────────────────────────────────────────────────────────────────
用法

    #include <Terminal/Terminal.h>
    using namespace X_Y;

    Terminal::Init();                       // 引擎 / 工具启动时一次

    auto caps = Terminal::GetCaps();        // 缓存；Probe() 是现场查
    if (!caps.Tty)
        Terminal::Write("重定向输出，纯文本\n");

    auto style = Terminal::Style{}
                     .SetFg(Terminal::Color::BrightGreen)
                     .SetBold();

    Terminal::Write(Terminal::Paint("[ OK ] 完成", style) + "\n");

    // 真彩色会按终端能力自动降级
    Terminal::Write(Terminal::Paint("rgb", Terminal::Style{}
                                               .SetFg(Terminal::Rgb{255, 128, 0})));

    // 写文件前把转义去掉
    std::string plain = Terminal::StripAnsi(line);
────────────────────────────────────────────────────────────────
*/

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace X_Y::Terminal
{
    enum class Stream : std::uint8_t
    {
        Out,
        Err
    };

    enum class ColorDepth : std::uint8_t
    {
        None,       // 不发颜色
        Ansi16,     // 30-37 / 90-97
        Ansi256,    // 38;5;n
        TrueColor   // 38;2;r;g;b
    };

    enum class Color : std::uint8_t
    {
        Default,
        Black,
        Red,
        Green,
        Yellow,
        Blue,
        Magenta,
        Cyan,
        White,
        BrightBlack,
        BrightRed,
        BrightGreen,
        BrightYellow,
        BrightBlue,
        BrightMagenta,
        BrightCyan,
        BrightWhite
    };

    struct Rgb
    {
        std::uint8_t R = 0;
        std::uint8_t G = 0;
        std::uint8_t B = 0;
    };

    struct Size
    {
        std::uint16_t Columns = 0;
        std::uint16_t Rows = 0;
    };

    struct Caps
    {
        bool       Tty   = false;
        bool       Ansi  = false;
        bool       Utf8  = false;
        ColorDepth Depth = ColorDepth::None;
        Size       Window{};
        std::string TermName;   // "windows-terminal" / "xterm-256color" / ...
    };

    // 样式描述。同时设了枚举色和 RGB 时，后设的赢（setter 会清掉另一个）。
    struct Style
    {
        std::optional<Color> Foreground;
        std::optional<Color> Background;
        std::optional<Rgb>   ForegroundRgb;
        std::optional<Rgb>   BackgroundRgb;

        bool Bold      = false;
        bool Dim       = false;
        bool Italic    = false;
        bool Underline = false;
        bool Reverse   = false;
        bool Strike    = false;

        Style &SetFg(Color color)
        {
            Foreground = color;
            ForegroundRgb.reset();
            return *this;
        }

        Style &SetFg(Rgb color)
        {
            ForegroundRgb = color;
            Foreground.reset();
            return *this;
        }

        Style &SetBg(Color color)
        {
            Background = color;
            BackgroundRgb.reset();
            return *this;
        }

        Style &SetBg(Rgb color)
        {
            BackgroundRgb = color;
            Background.reset();
            return *this;
        }

        Style &SetBold(bool on = true)      { Bold = on; return *this; }
        Style &SetDim(bool on = true)       { Dim = on; return *this; }
        Style &SetItalic(bool on = true)    { Italic = on; return *this; }
        Style &SetUnderline(bool on = true) { Underline = on; return *this; }
        Style &SetReverse(bool on = true)   { Reverse = on; return *this; }
        Style &SetStrike(bool on = true)    { Strike = on; return *this; }
    };

    // ── 探测与生命周期 ──────────────────────────────────────────────

    // 现场查当前状态，不改任何东西。注意：Windows 上 Init() 之前 ANSI 通常是关的。
    Caps Probe(Stream stream = Stream::Out);

    // 幂等。Windows 上开 VT + UTF-8 代码页；返回 stdout 的 ANSI 是否可用。
    // 第一次 GetCaps / Write 会自动调它；也可以显式提前调。
    bool Init();

    // 还原控制台模式（测试 / 命令行工具退出前用；引擎可以不管）。
    void Shutdown();

    // Init 缓存下来的能力。第一次调用会触发 Init。
    Caps GetCaps(Stream stream = Stream::Out);

    // stdin 和 stdout 都连着终端。Shell 用它决定要不要打提示符。
    bool IsInteractive();

    // ── 颜色开关 ────────────────────────────────────────────────────

    // 非 tty 或终端没有颜色能力时恒为 false；NO_COLOR 存在时也为 false。
    bool ColorsEnabled(Stream stream = Stream::Out);

    // 进程级覆盖：false 强制关；true 只忽略 NO_COLOR，不伪造终端能力。
    void SetColorsEnabled(bool on);

    // ── 纯字符串（不碰终端，可单测）────────────────────────────────

    std::string Sgr(const Style &style, ColorDepth depth);
    std::string Paint(std::string_view text, const Style &style, ColorDepth depth);

    // 按 stream 的实际能力拼；不能上色就原样返回 text。
    std::string Paint(std::string_view text, const Style &style,
                      Stream stream = Stream::Out);

    // 去掉 CSI / OSC / 两字节 escape。写文件、算宽度前用。
    std::string StripAnsi(std::string_view text);

    // ── 输出 ────────────────────────────────────────────────────────

    // 写 UTF-8 字节并 flush。非 tty 也照写（日志重定向靠它）。
    void Write(std::string_view text, Stream stream = Stream::Out);
    void Flush(Stream stream = Stream::Out);

    // ── 屏幕与光标 ──────────────────────────────────────────────────
    // 只有 tty 且 ANSI 可用时才发序列；否则返回 false，一个字节都不写。
    // 老 Windows 没有 VT 时也是 false，不会花屏。

    bool Clear(Stream stream = Stream::Out);                 // 清屏 + 光标归位
    bool ClearLine(Stream stream = Stream::Out);             // 清当前行 + 回到行首
    bool ClearLines(int count, Stream stream = Stream::Out); // 含当前行，往上 count 行

    bool MoveTo(int row, int column, Stream stream = Stream::Out);   // 1-based
    bool MoveUp(int count = 1, Stream stream = Stream::Out);
    bool MoveDown(int count = 1, Stream stream = Stream::Out);
    bool MoveLeft(int count = 1, Stream stream = Stream::Out);
    bool MoveRight(int count = 1, Stream stream = Stream::Out);

    bool SetCursorVisible(bool visible, Stream stream = Stream::Out);
    bool SaveCursor(Stream stream = Stream::Out);      // DECSC
    bool RestoreCursor(Stream stream = Stream::Out);   // DECRC

    // OSC 0；标题里的 ASCII 控制字符会被换成空格。
    bool SetTitle(std::string_view title);

    // ── 尺寸 ────────────────────────────────────────────────────────

    // 实时查；查不到回落到 GetCaps().Window；非 tty 返回 {0, 0}。
    Size GetSize(Stream stream = Stream::Out);
}
