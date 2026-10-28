#include <Terminal/Terminal.h>

#include <string>
#include <string_view>

namespace X_Y::Terminal
{
namespace
{
    bool Emit(Stream stream, std::string_view sequence)
    {
        const Caps caps = GetCaps(stream);
        if (!caps.Tty || !caps.Ansi)
            return false;

        Write(sequence, stream);
        return true;
    }

    std::string CursorMove(char finalByte, int count)
    {
        std::string sequence = "\x1b[";
        sequence += std::to_string(count);
        sequence += finalByte;
        return sequence;
    }
} // namespace

bool Clear(Stream stream)
{
    return Emit(stream, "\x1b[2J\x1b[H");
}

bool ClearLine(Stream stream)
{
    return Emit(stream, "\r\x1b[2K");
}

bool ClearLines(int count, Stream stream)
{
    if (count <= 0)
        return false;

    std::string sequence = "\r\x1b[2K";
    for (int i = 1; i < count; ++i)
        sequence += "\x1b[1A\x1b[2K";

    return Emit(stream, sequence);
}

bool MoveTo(int row, int column, Stream stream)
{
    if (row < 1 || column < 1)
        return false;

    return Emit(stream, "\x1b[" + std::to_string(row) + ";" +
                            std::to_string(column) + "H");
}

bool MoveUp(int count, Stream stream)
{
    if (count <= 0)
        return false;
    return Emit(stream, CursorMove('A', count));
}

bool MoveDown(int count, Stream stream)
{
    if (count <= 0)
        return false;
    return Emit(stream, CursorMove('B', count));
}

bool MoveLeft(int count, Stream stream)
{
    if (count <= 0)
        return false;
    return Emit(stream, CursorMove('D', count));
}

bool MoveRight(int count, Stream stream)
{
    if (count <= 0)
        return false;
    return Emit(stream, CursorMove('C', count));
}

bool SetCursorVisible(bool visible, Stream stream)
{
    return Emit(stream, visible ? "\x1b[?25h" : "\x1b[?25l");
}

bool SaveCursor(Stream stream)
{
    return Emit(stream, "\x1b" "7");
}

bool RestoreCursor(Stream stream)
{
    return Emit(stream, "\x1b" "8");
}

bool SetTitle(std::string_view title)
{
    const Caps caps = GetCaps(Stream::Out);
    if (!caps.Tty || !caps.Ansi)
        return false;

    // 只放行 UTF-8 字节；ASCII 控制字符和 DEL 换成空格，避免提前结束 OSC。
    std::string sanitized;
    sanitized.reserve(title.size());
    for (unsigned char c : title)
    {
        if (c < 0x20 || c == 0x7F)
            sanitized += ' ';
        else
            sanitized += static_cast<char>(c);
    }

    Write("\x1b]0;" + sanitized + "\x07", Stream::Out);
    return true;
}

} // namespace X_Y::Terminal
