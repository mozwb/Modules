#include <Input/Console.h>
#include <Terminal/Terminal.h>

#include <cstdio>
#include <string>

namespace
{
    const char *SpecialName(unsigned int code)
    {
        using Key = X_Y::Input::Key;

        switch (code)
        {
        case Key::Escape:    return "Escape";
        case Key::Enter:     return "Enter";
        case Key::Tab:       return "Tab";
        case Key::Backspace: return "Backspace";
        case Key::Insert:    return "Insert";
        case Key::Delete:    return "Delete";
        case Key::Right:     return "Right";
        case Key::Left:      return "Left";
        case Key::Down:      return "Down";
        case Key::Up:        return "Up";
        case Key::PageUp:    return "PageUp";
        case Key::PageDown:  return "PageDown";
        case Key::Home:      return "Home";
        case Key::End:       return "End";
        default:             return "Special";
        }
    }

    const char *Name(const X_Y::Input::KeyEvent &key)
    {
        using KeyKind = X_Y::Input::KeyKind;

        switch (key.Type)
        {
        case KeyKind::Character: return "Character";
        case KeyKind::Control:   return "Control";
        case KeyKind::Special:   return SpecialName(key.Code);
        case KeyKind::Eof:       return "Eof";
        case KeyKind::Unknown:   return "Unknown";
        }
        return "?";
    }

    std::string Hex(const std::string &text)
    {
        std::string result;
        for (unsigned char c : text)
        {
            char buffer[8] = {};
            std::snprintf(buffer, sizeof(buffer), "%02X ", c);
            result += buffer;
        }
        return result;
    }
} // namespace

int main()
{
    using namespace X_Y;

    Terminal::Init();

    if (!Terminal::IsInteractive())
    {
        std::printf("not interactive: KeyDemo 要在真终端里跑\n");
        return 0;
    }

    Terminal::Write("KeyDemo: 按键看名字，Ctrl+D 退出\n");

    {
        Console::RawMode raw;
        if (!raw.Ok())
        {
            Terminal::Write("进入 raw mode 失败\n");
            return 1;
        }

        for (;;)
        {
            const auto key = Console::ReadKey();
            if (key.Type == Console::KeyKind::Eof)
                break;

            if (key.Type == Console::KeyKind::Control && key.Text.size() == 1 &&
                key.Text[0] == '\x04')
                break;

            std::string line = std::string("key: ") + Name(key);

            if (key.Type == Console::KeyKind::Character)
                line += " [" + key.Text + "]";
            else if (key.Type == Console::KeyKind::Control)
                line += " [" + Hex(key.Text) + "]";
            else if (key.Type == Console::KeyKind::Special)
                line += " (" + std::to_string(key.Code) + ")";

            Terminal::Write(line + "\n");
        }
    }

    Terminal::Write("bye\n");
    return 0;
}
