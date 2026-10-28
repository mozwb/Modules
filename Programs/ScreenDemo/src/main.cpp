#include <Terminal/Terminal.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

namespace
{
    void Sleep(int milliseconds)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
} // namespace

int main()
{
    using namespace X_Y;

    Terminal::Init();

    // 非 tty：所有屏幕操作都应该返回 false，而且不写任何字节。
    if (!Terminal::IsInteractive())
    {
        bool any = false;
        any = Terminal::Clear() || any;
        any = Terminal::ClearLine() || any;
        any = Terminal::ClearLines(2) || any;
        any = Terminal::MoveTo(1, 1) || any;
        any = Terminal::MoveUp() || any;
        any = Terminal::MoveDown() || any;
        any = Terminal::MoveLeft() || any;
        any = Terminal::MoveRight() || any;
        any = Terminal::SetCursorVisible(false) || any;
        any = Terminal::SaveCursor() || any;
        any = Terminal::RestoreCursor() || any;
        any = Terminal::SetTitle("should not happen") || any;

        std::printf("non-interactive: all screen ops false = %d\n", any ? 0 : 1);
        return any ? 1 : 0;
    }

    const auto titleStyle = Terminal::Style{}
                                .SetFg(Terminal::Color::BrightGreen)
                                .SetBold();
    const auto barStyle = Terminal::Style{}.SetFg(Terminal::Color::BrightCyan);

    Terminal::SetTitle("X_Y ScreenDemo");
    Terminal::Clear();
    Terminal::SetCursorVisible(false);

    Terminal::MoveTo(1, 1);
    Terminal::Write(Terminal::Paint("X_Y Terminal screen demo", titleStyle) + "\n");

    for (int i = 0; i <= 10; ++i)
    {
        Terminal::MoveTo(3, 1);
        Terminal::ClearLine();

        std::string bar(static_cast<std::size_t>(i), '#');
        bar.append(static_cast<std::size_t>(10 - i), '.');

        Terminal::Write(Terminal::Paint("进度 [" + bar + "] " +
                                            std::to_string(i * 10) + "%",
                                        barStyle));
        Sleep(80);
    }

    Terminal::MoveTo(5, 1);
    Terminal::Write("save cursor here: ");
    Terminal::SaveCursor();
    Terminal::Write("(临时文字)");
    Terminal::RestoreCursor();
    Terminal::Write("OK");

    Terminal::MoveTo(6, 1);
    Terminal::Write("DECSC / DECRC 正常");

    Terminal::SetCursorVisible(true);
    Terminal::MoveTo(8, 1);
    Terminal::Write("2 秒后清屏 ...");
    Sleep(2000);

    Terminal::Clear();
    Terminal::MoveTo(1, 1);
    Terminal::Write("done\n");

    return 0;
}
