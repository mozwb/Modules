#include <Terminal/Terminal.h>

#include <cstdio>
#include <fstream>
#include <string>

namespace
{
    std::string Readable(std::string text)
    {
        std::string result;
        result.reserve(text.size());
        for (char c : text)
        {
            if (c == '\x1b')
                result += "\\x1b";
            else
                result += c;
        }
        return result;
    }

    void DumpCaps(const char *name, X_Y::Terminal::Stream stream)
    {
        using namespace X_Y;

        const Terminal::Caps caps = Terminal::GetCaps(stream);
        std::printf("%s: tty=%d ansi=%d depth=%d utf8=%d size=%ux%u term=%s\n",
                    name,
                    caps.Tty ? 1 : 0,
                    caps.Ansi ? 1 : 0,
                    static_cast<int>(caps.Depth),
                    caps.Utf8 ? 1 : 0,
                    static_cast<unsigned>(caps.Window.Columns),
                    static_cast<unsigned>(caps.Window.Rows),
                    caps.TermName.empty() ? "-" : caps.TermName.c_str());
    }

    int Check(bool ok, const char *name)
    {
        std::printf("%-18s %s\n", name, ok ? "ok" : "FAIL");
        return ok ? 0 : 1;
    }
} // namespace

int main(int argc, char **argv)
{
    using namespace X_Y;

    std::string reportPath;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--report" && i + 1 < argc)
            reportPath = argv[++i];
    }

    Terminal::Init();

    DumpCaps("stdout", Terminal::Stream::Out);
    DumpCaps("stderr", Terminal::Stream::Err);

    const Terminal::Size live = Terminal::GetSize();
    std::printf("live size: %ux%u\n", static_cast<unsigned>(live.Columns),
                static_cast<unsigned>(live.Rows));

    const auto green = Terminal::Style{}
                           .SetFg(Terminal::Color::BrightGreen)
                           .SetBold();

    Terminal::Write(Terminal::Paint("[ OK ] Terminal 就绪", green) + "\n");

    const auto orange = Terminal::Style{}.SetFg(Terminal::Rgb{255, 128, 0});
    Terminal::Write(Terminal::Paint("真彩色 -> 256 -> 16 自动降级", orange) + "\n");

    // 下面这些不看终端也能验：纯函数按指定深度出串。
    const std::string truecolor =
        Terminal::Paint("hello", green, Terminal::ColorDepth::TrueColor);
    const std::string ansi256 =
        Terminal::Paint("hello", green, Terminal::ColorDepth::Ansi256);
    const std::string ansi16 =
        Terminal::Paint("hello", green, Terminal::ColorDepth::Ansi16);
    const std::string plain =
        Terminal::Paint("hello", green, Terminal::ColorDepth::None);

    // 命名色不降级，RGB 才走 256 / 16 的最近色。
    const std::string rgbTrue =
        Terminal::Paint("hello", orange, Terminal::ColorDepth::TrueColor);
    const std::string rgb256 =
        Terminal::Paint("hello", orange, Terminal::ColorDepth::Ansi256);
    const std::string rgb16 =
        Terminal::Paint("hello", orange, Terminal::ColorDepth::Ansi16);

    std::printf("sgr truecolor : %s\n", Readable(truecolor).c_str());
    std::printf("sgr 256       : %s\n", Readable(ansi256).c_str());
    std::printf("sgr 16        : %s\n", Readable(ansi16).c_str());
    std::printf("sgr none      : %s\n", Readable(plain).c_str());
    std::printf("rgb truecolor : %s\n", Readable(rgbTrue).c_str());
    std::printf("rgb 256       : %s\n", Readable(rgb256).c_str());
    std::printf("rgb 16        : %s\n", Readable(rgb16).c_str());

    int failures = 0;
    failures += Check(Terminal::StripAnsi(truecolor) == "hello", "strip truecolor");
    failures += Check(Terminal::StripAnsi(ansi256) == "hello", "strip 256");
    failures += Check(Terminal::StripAnsi(ansi16) == "hello", "strip 16");
    failures += Check(plain == "hello", "none is plain");
    failures += Check(Terminal::Sgr(Terminal::Style{}, Terminal::ColorDepth::TrueColor).empty(),
                      "empty style");

    // 255,128,0 -> 256 的 208 号（6x6x6 立方），16 色里最接近亮黄 93。
    failures += Check(rgbTrue.rfind("\x1b[38;2;255;128;0m", 0) == 0, "rgb truecolor sgr");
    failures += Check(rgb256.rfind("\x1b[38;5;208m", 0) == 0, "rgb 256 cube");
    failures += Check(rgb16.rfind("\x1b[93m", 0) == 0, "rgb 16 nearest");

    // 重定向时（stdout 不是 tty）这里必须是纯文本，文件里不能有 ESC。
    const std::string runtime = Terminal::Paint("redirect me", green);
    failures += Check(runtime.find('\x1b') == std::string::npos || Terminal::GetCaps().Tty,
                      "no esc when redirected");

    if (!reportPath.empty())
    {
        const Terminal::Caps caps = Terminal::GetCaps();
        std::ofstream report(reportPath);
        report << "tty=" << (caps.Tty ? 1 : 0)
               << " ansi=" << (caps.Ansi ? 1 : 0)
               << " depth=" << static_cast<int>(caps.Depth)
               << " utf8=" << (caps.Utf8 ? 1 : 0)
               << " size=" << caps.Window.Columns << "x" << caps.Window.Rows
               << " term=" << caps.TermName
               << " failures=" << failures << "\n";
    }

    Terminal::Shutdown();

    std::printf("failures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
