#include "UI/Component/TextInput.h"
#include "Input/Input.h"
#include "Widget/FontLibrary.h"

namespace X_Y
{

    // ── 工具：宽字符 → UTF-8 ──
    // 输入框存 UTF-8（std::string），中文字符按 UTF-8 编码插入，不能强转单字节。

    std::string TextInput::Utf8FromWide(wchar_t ch)
    {
        if (ch < 0x80)
        {
            return std::string(1, (char)ch);
        }
        if (ch < 0x800)
        {
            char b[2] = {
                (char)(0xC0 | (ch >> 6)),
                (char)(0x80 | (ch & 0x3F)),
            };
            return std::string(b, 2);
        }
        char b[3] = {
            (char)(0xE0 | (ch >> 12)),
            (char)(0x80 | ((ch >> 6) & 0x3F)),
            (char)(0x80 | (ch & 0x3F)),
        };
        return std::string(b, 3);
    }

    void TextInput::SetText(const char *text)
    {
        m_Text = text ? text : "";
        m_CursorPos = (int)m_Text.size();
        RequestRepaint();
        NotifyTextChange();
    }

    void TextInput::SetText(const std::string &text)
    {
        m_Text = text;
        m_CursorPos = (int)m_Text.size();
        RequestRepaint();
        NotifyTextChange();
    }

    void TextInput::OnPaint(Canvas &canvas)
    {
        // 组件坐标一律【相对所属容器】(0,0 起画)；位置偏移由宿主压 canvas origin 处理。
        const int x = 0, y = 0, w = GetWidth(), h = GetHeight();

        canvas.FillRect(x, y, w, h, IsFocused() ? 0xFFFFFFFF : 0xFFF0F0F0);

        canvas.FillRect(x, y, w, 1, m_BorderColor);
        canvas.FillRect(x, y + h - 1, w, 1, m_BorderColor);
        canvas.FillRect(x, y, 1, h, m_BorderColor);
        canvas.FillRect(x + w - 1, y, 1, h, m_BorderColor);

        int textX = x + 4;
        int textY = y + (h - 14) / 2;
        auto &font = FontLibrary::Instance().GetDefault();
        if (!m_Text.empty())
        {
            canvas.DrawText(font, textX, textY, m_Text.c_str(), m_TextColor);
        }
        else if (!this->IsFocused() && !m_Placeholder.empty())
        {
            canvas.DrawText(font, textX, textY, m_Placeholder.c_str(), 0xFFAAAAAA);
        }

        if (IsFocused())
        {
            std::string before = m_Text.substr(0, m_CursorPos);
            // 用实际字体测宽（中文/全角宽度不同，不能硬编码 *8）
            int cx = textX + font.MeasureText(before.c_str());
            canvas.FillRect(cx, textY, 1, 14, 0xFF000000);
        }
    }

    void TextInput::OnInput(UIInputEvent &e)
    {
        auto *ke = dynamic_cast<UIKeyEvent *>(&e);
        if (!ke)
            return; // 文本输入只关心键盘

        if (m_ReadOnly)
            return;

        // 字符输入
        if (ke->isChar)
        {
            const wchar_t ch = ke->ch;
            if (ch >= 32 && ch != 127)
            {
                std::string utf8 = Utf8FromWide(ch);
                m_Text.insert(m_CursorPos, utf8);
                m_CursorPos += (int)utf8.size();
                RequestRepaint();
                NotifyTextChange();
            }
            return;
        }

        Input::KeyCode key = ke->key;
        bool changed = false;

        // 光标按 UTF-8 字符边界步进
        auto prevCharStart = [](const std::string &s, int pos)
        {
            if (pos <= 0)
                return 0;
            int p = pos - 1;
            while (p > 0 && ((unsigned char)s[p] & 0xC0) == 0x80)
                p--;
            return p;
        };
        auto nextCharEnd = [](const std::string &s, int pos)
        {
            if (pos >= (int)s.size())
                return (int)s.size();
            int p = pos;
            p++;
            while (p < (int)s.size() && ((unsigned char)s[p] & 0xC0) == 0x80)
                p++;
            return p;
        };

        switch (key)
        {
        case Input::Key::Left:
            m_CursorPos = prevCharStart(m_Text, m_CursorPos);
            break;
        case Input::Key::Right:
            m_CursorPos = nextCharEnd(m_Text, m_CursorPos);
            break;
        case Input::Key::Home:
            m_CursorPos = 0;
            break;
        case Input::Key::Enter:
            if (OnEnter)
                OnEnter();
            break;
        case Input::Key::End:
            m_CursorPos = (int)m_Text.size();
            break;
        case Input::Key::Delete:
        {
            if (m_CursorPos < (int)m_Text.size())
            {
                int end = nextCharEnd(m_Text, m_CursorPos);
                m_Text.erase(m_CursorPos, end - m_CursorPos);
                changed = true;
            }
            break;
        }
        case Input::Key::Backspace:
        {
            if (m_CursorPos > 0)
            {
                int start = prevCharStart(m_Text, m_CursorPos);
                m_Text.erase(start, m_CursorPos - start);
                m_CursorPos = start;
                changed = true;
            }
            break;
        }
        }

        if (changed)
        {
            RequestRepaint();
            NotifyTextChange();
        }
        else
        {
            RequestRepaint();
        }
    }

    void TextInput::NotifyTextChange()
    {
        if (OnTextChange)
            OnTextChange(m_Text);
    }

}
