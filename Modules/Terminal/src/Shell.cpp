#include <Terminal/Shell.h>
#include <Terminal/Terminal.h>

#include <Input/Console.h>

#include <Core/Core.h>

#include <cctype>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace X_Y::Shell
{
namespace
{
    constexpr std::size_t kHistoryLimit = 100;

    std::size_t PreviousChar(const std::string &text, std::size_t position)
    {
        if (position == 0)
            return 0;

        std::size_t index = position - 1;
        while (index > 0 &&
               (static_cast<unsigned char>(text[index]) & 0xC0) == 0x80)
            --index;

        return index;
    }

    std::size_t NextChar(const std::string &text, std::size_t position)
    {
        if (position >= text.size())
            return text.size();

        std::size_t index = position + 1;
        while (index < text.size() &&
               (static_cast<unsigned char>(text[index]) & 0xC0) == 0x80)
            ++index;

        return index;
    }

    bool DecodeUtf8(std::string_view text, std::size_t offset,
                    std::uint32_t &codepoint, std::size_t &length)
    {
        if (offset >= text.size())
            return false;

        const unsigned char first = static_cast<unsigned char>(text[offset]);
        if (first < 0x80)
        {
            codepoint = first;
            length    = 1;
            return true;
        }

        int          count = 0;
        std::uint32_t value = 0;

        if ((first & 0xE0) == 0xC0)
        {
            count = 2;
            value = first & 0x1F;
        }
        else if ((first & 0xF0) == 0xE0)
        {
            count = 3;
            value = first & 0x0F;
        }
        else if ((first & 0xF8) == 0xF0)
        {
            count = 4;
            value = first & 0x07;
        }
        else
        {
            return false;
        }

        if (offset + static_cast<std::size_t>(count) > text.size())
            return false;

        for (int i = 1; i < count; ++i)
        {
            const unsigned char current =
                static_cast<unsigned char>(text[offset + static_cast<std::size_t>(i)]);
            if ((current & 0xC0) != 0x80)
                return false;
            value = (value << 6) | (current & 0x3F);
        }

        codepoint = value;
        length    = static_cast<std::size_t>(count);
        return true;
    }

    bool IsZeroWidth(std::uint32_t codepoint)
    {
        if (codepoint >= 0x0300 && codepoint <= 0x036F)
            return true;
        if (codepoint >= 0x1AB0 && codepoint <= 0x1AFF)
            return true;
        if (codepoint >= 0x1DC0 && codepoint <= 0x1DFF)
            return true;
        if (codepoint >= 0x20D0 && codepoint <= 0x20FF)
            return true;
        if (codepoint >= 0xFE20 && codepoint <= 0xFE2F)
            return true;
        if (codepoint >= 0xFE00 && codepoint <= 0xFE0F)
            return true;
        if (codepoint == 0x200B || codepoint == 0x200C || codepoint == 0x200D ||
            codepoint == 0x2060 || codepoint == 0xFEFF)
            return true;
        if (codepoint >= 0x1F3FB && codepoint <= 0x1F3FF)
            return true;

        return false;
    }

    bool IsWide(std::uint32_t codepoint)
    {
        if (codepoint >= 0x1100 && codepoint <= 0x115F)
            return true;
        if (codepoint == 0x2329 || codepoint == 0x232A)
            return true;
        if (codepoint >= 0x2E80 && codepoint <= 0x303E)
            return true;
        if (codepoint >= 0x3041 && codepoint <= 0x33FF)
            return true;
        if (codepoint >= 0x3400 && codepoint <= 0x4DBF)
            return true;
        if (codepoint >= 0x4E00 && codepoint <= 0x9FFF)
            return true;
        if (codepoint >= 0xA000 && codepoint <= 0xA4CF)
            return true;
        if (codepoint >= 0xA960 && codepoint <= 0xA97F)
            return true;
        if (codepoint >= 0xAC00 && codepoint <= 0xD7A3)
            return true;
        if (codepoint >= 0xF900 && codepoint <= 0xFAFF)
            return true;
        if (codepoint >= 0xFE10 && codepoint <= 0xFE19)
            return true;
        if (codepoint >= 0xFE30 && codepoint <= 0xFE6F)
            return true;
        if (codepoint >= 0xFF00 && codepoint <= 0xFF60)
            return true;
        if (codepoint >= 0xFFE0 && codepoint <= 0xFFE6)
            return true;
        if (codepoint >= 0x1B000 && codepoint <= 0x1B2FF)
            return true;
        if (codepoint >= 0x1F200 && codepoint <= 0x1F2FF)
            return true;
        if (codepoint >= 0x1F300 && codepoint <= 0x1FAFF)
            return true;
        if (codepoint >= 0x20000 && codepoint <= 0x3FFFD)
            return true;

        return false;
    }

    int CodepointWidth(std::uint32_t codepoint)
    {
        if (codepoint < 0x20 || (codepoint >= 0x7F && codepoint < 0xA0))
            return 0;
        if (IsZeroWidth(codepoint))
            return 0;
        if (IsWide(codepoint))
            return 2;
        return 1;
    }

    int DisplayWidth(std::string_view text)
    {
        int width = 0;

        std::size_t offset = 0;
        while (offset < text.size())
        {
            std::uint32_t codepoint = 0;
            std::size_t   length    = 0;

            if (!DecodeUtf8(text, offset, codepoint, length))
            {
                ++offset;
                ++width;
                continue;
            }

            width += CodepointWidth(codepoint);
            offset += length;
        }

        return width;
    }

    std::size_t PreviousWord(const std::string &text, std::size_t position)
    {
        while (position > 0)
        {
            const std::size_t previous = PreviousChar(text, position);
            const unsigned char current =
                static_cast<unsigned char>(text[previous]);
            if (!std::isspace(current))
                break;
            position = previous;
        }

        while (position > 0)
        {
            const std::size_t previous = PreviousChar(text, position);
            const unsigned char current =
                static_cast<unsigned char>(text[previous]);
            if (std::isspace(current))
                break;
            position = previous;
        }

        return position;
    }
} // namespace

Shell::Shell(Command::Registry &registry)
    : _registry(registry)
{
}

void Shell::SetPrompt(std::string prompt)
{
    _prompt = std::move(prompt);
}

std::string Shell::GetPrompt() const
{
    return _prompt;
}

void Shell::Stop()
{
    _stopRequested = true;
}

int Shell::Run()
{
    _stopRequested = false;
    _lastExitCode  = 0;

    const bool interactive = Terminal::IsInteractive();
    const bool editing =
        interactive && Terminal::GetCaps(Terminal::Stream::Out).Ansi;

    while (!_stopRequested)
    {
        std::string line;

        if (editing)
        {
            if (!ReadLineInteractive(line))
                break;
        }
        else
        {
            if (interactive)
                Terminal::Write(_prompt);

            if (!std::getline(std::cin, line))
            {
                if (interactive)
                    Terminal::Write("\n");
                break;
            }

            if (!line.empty() && line.back() == '\r')
                line.pop_back();
        }

        const int code = Execute(line);
        if (code >= 0)
            _lastExitCode = code;
    }

    return _lastExitCode;
}

bool Shell::ReadLineInteractive(std::string &line)
{
    Console::RawMode raw;
    if (!raw.Ok())
    {
        PrintError("无法进入 raw mode");
        return false;
    }

    std::string buffer;
    std::size_t cursor         = 0;
    std::size_t historyIndex   = _history.size();
    std::string draft;
    bool        browsingHistory = false;

    Terminal::Write(_prompt);

    for (;;)
    {
        const Input::KeyEvent key = Console::ReadKey();

        if (key.Type == Input::KeyKind::Eof)
        {
            Terminal::Write("\n");
            return false;
        }

        if (key.Type == Input::KeyKind::Character)
        {
            buffer.insert(cursor, key.Text);
            cursor += key.Text.size();
            RedrawLine(buffer, cursor);
            continue;
        }

        if (key.Type == Input::KeyKind::Control)
        {
            const unsigned char control =
                key.Text.empty() ? 0
                                 : static_cast<unsigned char>(key.Text.front());

            switch (control)
            {
            case 0x03: // Ctrl+C
                Terminal::Write("^C\n");
                buffer.clear();
                cursor       = 0;
                historyIndex = _history.size();
                draft.clear();
                browsingHistory = false;
                Terminal::Write(_prompt);
                break;

            case 0x04: // Ctrl+D
                if (buffer.empty())
                {
                    Terminal::Write("\n");
                    return false;
                }
                break;

            case 0x01: // Ctrl+A
                cursor = 0;
                RedrawLine(buffer, cursor);
                break;

            case 0x05: // Ctrl+E
                cursor = buffer.size();
                RedrawLine(buffer, cursor);
                break;

            case 0x0B: // Ctrl+K
                buffer.erase(cursor);
                RedrawLine(buffer, cursor);
                break;

            case 0x15: // Ctrl+U
                buffer.erase(0, cursor);
                cursor = 0;
                RedrawLine(buffer, cursor);
                break;

            case 0x17: // Ctrl+W
            {
                const std::size_t start = PreviousWord(buffer, cursor);
                buffer.erase(start, cursor - start);
                cursor = start;
                RedrawLine(buffer, cursor);
                break;
            }

            case 0x0C: // Ctrl+L
                if (Terminal::Clear())
                {
                    Terminal::MoveTo(1, 1);
                    RedrawLine(buffer, cursor);
                }
                break;

            default:
                break;
            }

            continue;
        }

        if (key.Type != Input::KeyKind::Special)
            continue;

        switch (key.Code)
        {
        case Input::Key::Enter:
            Terminal::Write("\n");
            line = buffer;
            if (!Utils::IsBlank(buffer))
                AddHistory(buffer);
            return true;

        case Input::Key::Backspace:
            if (cursor > 0)
            {
                const std::size_t start = PreviousChar(buffer, cursor);
                buffer.erase(start, cursor - start);
                cursor = start;
                RedrawLine(buffer, cursor);
            }
            break;

        case Input::Key::Delete:
            if (cursor < buffer.size())
            {
                const std::size_t end = NextChar(buffer, cursor);
                buffer.erase(cursor, end - cursor);
                RedrawLine(buffer, cursor);
            }
            break;

        case Input::Key::Left:
            if (cursor > 0)
            {
                cursor = PreviousChar(buffer, cursor);
                RedrawLine(buffer, cursor);
            }
            break;

        case Input::Key::Right:
            if (cursor < buffer.size())
            {
                cursor = NextChar(buffer, cursor);
                RedrawLine(buffer, cursor);
            }
            break;

        case Input::Key::Home:
            cursor = 0;
            RedrawLine(buffer, cursor);
            break;

        case Input::Key::End:
            cursor = buffer.size();
            RedrawLine(buffer, cursor);
            break;

        case Input::Key::Up:
            if (!_history.empty())
            {
                if (!browsingHistory)
                {
                    draft           = buffer;
                    browsingHistory = true;
                    historyIndex    = _history.size() - 1;
                }
                else if (historyIndex > 0)
                {
                    --historyIndex;
                }
                else
                {
                    break;
                }

                buffer = _history[historyIndex];
                cursor = buffer.size();
                RedrawLine(buffer, cursor);
            }
            break;

        case Input::Key::Down:
            if (!browsingHistory)
                break;

            if (historyIndex + 1 < _history.size())
            {
                ++historyIndex;
                buffer = _history[historyIndex];
            }
            else
            {
                buffer          = draft;
                browsingHistory = false;
                historyIndex    = _history.size();
            }

            cursor = buffer.size();
            RedrawLine(buffer, cursor);
            break;

        case Input::Key::Escape:
        default:
            break;
        }
    }
}

void Shell::AddHistory(std::string line)
{
    if (line.empty())
        return;

    if (!_history.empty() && _history.back() == line)
        return;

    _history.push_back(std::move(line));

    if (_history.size() > kHistoryLimit)
        _history.erase(_history.begin());
}

void Shell::RedrawLine(const std::string &buffer, std::size_t cursor) const
{
    if (!Terminal::ClearLine())
        return;

    Terminal::Write(_prompt);
    Terminal::Write(buffer);

    const int suffix =
        DisplayWidth(std::string_view(buffer).substr(cursor));
    if (suffix > 0)
        Terminal::MoveLeft(suffix);
}

int Shell::Execute(std::string_view line)
{
    if (Utils::IsBlank(std::string(line)))
        return -1;

    Command::Args args;
    std::string   error;
    if (!Command::Parse(line, args, &error))
    {
        PrintError(error);
        return 2;
    }

    const std::string name = Utils::ToLower(args.Name);

    if (name == "exit" || name == "quit")
        return ExecuteExit(args);

    if (name == "help")
    {
        PrintHelp();
        return 0;
    }

    const Command::Result result = _registry.Run(args);
    if (!result.Error.empty())
        PrintError(result.Error);

    return result.ExitCode;
}

int Shell::ExecuteExit(const Command::Args &args)
{
    if (args.Items.size() > 1)
    {
        PrintError("exit / quit 只接受一个退出码");
        return 2;
    }

    int code = 0;
    if (!args.Items.empty())
    {
        const std::string &text = args.Items.front();

        char *end = nullptr;
        errno     = 0;
        const long value = std::strtol(text.c_str(), &end, 10);

        if (errno != 0 || end == text.c_str() || *end != '\0' ||
            value < 0 || value > 255)
        {
            PrintError("退出码必须是 0-255 的整数");
            return 2;
        }

        code = static_cast<int>(value);
    }

    _stopRequested = true;
    return code;
}

void Shell::PrintHelp()
{
    const std::vector<std::string> names = _registry.Names();

    std::string line = "commands:";
    if (names.empty())
    {
        line += " (none)";
    }
    else
    {
        for (const auto &name : names)
            line += " " + name;
    }

    Terminal::Write("built-in: help, exit [code], quit [code]\n");
    Terminal::Write(line + "\n");
}

void Shell::PrintError(std::string_view message)
{
    const Terminal::Style style =
        Terminal::Style{}.SetFg(Terminal::Color::BrightRed).SetBold();

    Terminal::Write(Terminal::Paint("[error] " + std::string(message), style) + "\n");
}

} // namespace X_Y::Shell
