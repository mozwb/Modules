#include <Input/Console.h>

#include <windows.h>

#include <cstddef>
#include <string>
#include <utility>

namespace X_Y::Console
{
namespace
{
    bool HasCtrl(DWORD state)
    {
        return (state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    }

    std::string Utf8FromWChar(wchar_t character)
    {
        char buffer[8] = {};
        const int length = ::WideCharToMultiByte(
            CP_UTF8, 0, &character, 1, buffer, static_cast<int>(sizeof(buffer)),
            nullptr, nullptr);

        if (length <= 0)
            return {};

        return std::string(buffer, static_cast<std::size_t>(length));
    }

    unsigned int SpecialKey(WORD key)
    {
        switch (key)
        {
        case VK_RETURN: return Input::Key::Enter;
        case VK_TAB:    return Input::Key::Tab;
        case VK_BACK:   return Input::Key::Backspace;
        case VK_DELETE: return Input::Key::Delete;
        case VK_ESCAPE: return Input::Key::Escape;
        case VK_UP:     return Input::Key::Up;
        case VK_DOWN:   return Input::Key::Down;
        case VK_LEFT:   return Input::Key::Left;
        case VK_RIGHT:  return Input::Key::Right;
        case VK_HOME:   return Input::Key::Home;
        case VK_END:    return Input::Key::End;
        case VK_PRIOR:  return Input::Key::PageUp;
        case VK_NEXT:   return Input::Key::PageDown;
        case VK_INSERT: return Input::Key::Insert;
        default:        return 0;
        }
    }
} // namespace

struct RawMode::Impl
{
    HANDLE Input   = INVALID_HANDLE_VALUE;
    DWORD  OldMode = 0;
    bool   Valid   = false;
};

RawMode::RawMode()
{
    auto impl = std::make_unique<Impl>();
    impl->Input = ::GetStdHandle(STD_INPUT_HANDLE);

    DWORD mode = 0;
    if (impl->Input != nullptr && impl->Input != INVALID_HANDLE_VALUE &&
        ::GetConsoleMode(impl->Input, &mode))
    {
        impl->OldMode = mode;

        DWORD raw = mode;
        raw &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
        raw |= ENABLE_EXTENDED_FLAGS;
        raw &= ~ENABLE_QUICK_EDIT_MODE;

        if (::SetConsoleMode(impl->Input, raw))
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
        ::SetConsoleMode(_impl->Input, _impl->OldMode);
}

Key ReadKey()
{
    HANDLE input = ::GetStdHandle(STD_INPUT_HANDLE);
    if (input == nullptr || input == INVALID_HANDLE_VALUE)
        return {KeyKind::Eof, 0, {}};

    INPUT_RECORD record{};
    DWORD        count = 0;

    while (::ReadConsoleInputW(input, &record, 1, &count))
    {
        if (count == 0 || record.EventType != KEY_EVENT)
            continue;

        const KEY_EVENT_RECORD &key = record.Event.KeyEvent;
        if (!key.bKeyDown)
            continue;

        if (HasCtrl(key.dwControlKeyState))
        {
            if (key.wVirtualKeyCode == 'C' || key.uChar.UnicodeChar == 0x03)
                return {KeyKind::Control, 0, std::string(1, '\x03')};
            if (key.wVirtualKeyCode == 'D' || key.uChar.UnicodeChar == 0x04)
                return {KeyKind::Control, 0, std::string(1, '\x04')};
            if (key.wVirtualKeyCode == 'L' || key.uChar.UnicodeChar == 0x0C)
                return {KeyKind::Control, 0, std::string(1, '\x0C')};

            if (key.wVirtualKeyCode >= 'A' && key.wVirtualKeyCode <= 'Z')
            {
                const char control = static_cast<char>(key.wVirtualKeyCode - 'A' + 1);
                return {KeyKind::Control, 0, std::string(1, control)};
            }
        }

        const unsigned int special = SpecialKey(key.wVirtualKeyCode);
        if (special != 0)
            return {KeyKind::Special, special, {}};

        const wchar_t character = key.uChar.UnicodeChar;
        if (character == 0)
            return {KeyKind::Unknown, 0, {}};

        if (character < 0x20)
            return {KeyKind::Control, 0,
                    std::string(1, static_cast<char>(character))};

        std::string text = Utf8FromWChar(character);
        if (text.empty())
            return {KeyKind::Unknown, 0, {}};

        return {KeyKind::Character, 0, std::move(text)};
    }

    return {KeyKind::Eof, 0, {}};
}

} // namespace X_Y::Console
