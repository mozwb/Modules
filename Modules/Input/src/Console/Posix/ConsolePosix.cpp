#include <Input/Console.h>

#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace X_Y::Console
{
namespace
{
    bool ReadByte(unsigned char &out)
    {
        for (;;)
        {
            const ssize_t count = ::read(STDIN_FILENO, &out, 1);
            if (count == 1)
                return true;
            if (count < 0 && errno == EINTR)
                continue;
            return false;
        }
    }

    bool WaitForInput(int milliseconds)
    {
        pollfd descriptor{STDIN_FILENO, POLLIN, 0};

        int result = 0;
        do
        {
            result = ::poll(&descriptor, 1, milliseconds);
        } while (result < 0 && errno == EINTR);

        return result > 0;
    }

    unsigned int SpecialKey(std::string_view sequence)
    {
        if (sequence == "A")
            return Input::Key::Up;
        if (sequence == "B")
            return Input::Key::Down;
        if (sequence == "C")
            return Input::Key::Right;
        if (sequence == "D")
            return Input::Key::Left;
        if (sequence == "H")
            return Input::Key::Home;
        if (sequence == "F")
            return Input::Key::End;

        if (sequence == "1~" || sequence == "7~")
            return Input::Key::Home;
        if (sequence == "2~")
            return Input::Key::Insert;
        if (sequence == "3~")
            return Input::Key::Delete;
        if (sequence == "4~" || sequence == "8~")
            return Input::Key::End;
        if (sequence == "5~")
            return Input::Key::PageUp;
        if (sequence == "6~")
            return Input::Key::PageDown;
        if (sequence == "Z")
            return Input::Key::Tab; // Shift+Tab 先当 Tab

        return 0;
    }
} // namespace

struct RawMode::Impl
{
    termios Old{};
    bool    Valid = false;
};

RawMode::RawMode()
{
    auto impl = std::make_unique<Impl>();

    if (::tcgetattr(STDIN_FILENO, &impl->Old) == 0)
    {
        termios raw = impl->Old;
        raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO | ISIG | IEXTEN);
        raw.c_iflag &= ~static_cast<tcflag_t>(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
        raw.c_cc[VMIN]  = 1;
        raw.c_cc[VTIME] = 0;

        if (::tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0)
        {
            impl->Valid = true;
            _ok         = true;
        }
    }

    _impl = std::move(impl);
}

RawMode::~RawMode()
{
    if (_impl && _impl->Valid)
        ::tcsetattr(STDIN_FILENO, TCSANOW, &_impl->Old);
}

Key ReadKey()
{
    unsigned char first = 0;
    if (!ReadByte(first))
        return {KeyKind::Eof, 0, {}};

    if (first == 0x1B)
    {
        if (!WaitForInput(30))
            return {KeyKind::Special, Input::Key::Escape, {}};

        unsigned char second = 0;
        if (!ReadByte(second))
            return {KeyKind::Special, Input::Key::Escape, {}};

        if (second == '[' || second == 'O')
        {
            std::string sequence;

            if (second == 'O')
            {
                unsigned char third = 0;
                if (ReadByte(third))
                    sequence += static_cast<char>(third);
            }
            else
            {
                for (;;)
                {
                    unsigned char current = 0;
                    if (!ReadByte(current))
                        break;

                    sequence += static_cast<char>(current);
                    if (current >= 0x40 && current <= 0x7E)
                        break;
                    if (sequence.size() >= 16)
                        break;
                }
            }

            const unsigned int special = SpecialKey(sequence);
            if (special != 0)
                return {KeyKind::Special, special, {}};

            return {KeyKind::Unknown, 0, {}};
        }

        return {KeyKind::Unknown, 0, {}};
    }

    if (first == '\r' || first == '\n')
        return {KeyKind::Special, Input::Key::Enter, {}};
    if (first == '\t')
        return {KeyKind::Special, Input::Key::Tab, {}};
    if (first == 0x7F || first == 0x08)
        return {KeyKind::Special, Input::Key::Backspace, {}};

    if (first < 0x20)
        return {KeyKind::Control, 0, std::string(1, static_cast<char>(first))};

    if (first < 0x80)
        return {KeyKind::Character, 0, std::string(1, static_cast<char>(first))};

    int length = 0;
    if ((first & 0xE0) == 0xC0)
        length = 2;
    else if ((first & 0xF0) == 0xE0)
        length = 3;
    else if ((first & 0xF8) == 0xF0)
        length = 4;
    else
        return {KeyKind::Unknown, 0, {}};

    std::string text(1, static_cast<char>(first));
    for (int i = 1; i < length; ++i)
    {
        unsigned char current = 0;
        if (!ReadByte(current))
            return {KeyKind::Unknown, 0, {}};
        if ((current & 0xC0) != 0x80)
            return {KeyKind::Unknown, 0, {}};

        text += static_cast<char>(current);
    }

    return {KeyKind::Character, 0, std::move(text)};
}

} // namespace X_Y::Console
