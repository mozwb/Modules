#include "../Platform.h"

#include <windows.h>

#include <cstdlib>
#include <string>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

namespace X_Y::Terminal::Detail
{
namespace
{
    HANDLE HandleOf(Stream stream)
    {
        return ::GetStdHandle(stream == Stream::Err ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    }

    bool QueryMode(HANDLE handle, DWORD &mode)
    {
        return handle != nullptr && handle != INVALID_HANDLE_VALUE &&
               ::GetConsoleMode(handle, &mode) != 0;
    }

    const char *EnvValue(const char *name)
    {
        const char *value = std::getenv(name);
        return value != nullptr ? value : "";
    }

    struct SavedConsole
    {
        DWORD OutMode        = 0;
        DWORD ErrMode        = 0;
        UINT  OutputCodePage = 0;
        bool  OutSaved       = false;
        bool  ErrSaved       = false;
        bool  CodePageSaved  = false;
    };

    SavedConsole g_saved;
} // namespace

Caps ProbePlatform(Stream stream)
{
    Caps caps;

    HANDLE handle = HandleOf(stream);
    DWORD  mode   = 0;
    caps.Tty      = QueryMode(handle, mode);

    if (!caps.Tty)
    {
        // 管道 / 文件：没有控制能力，但写出去的仍是 UTF-8 字节。
        caps.Utf8 = true;
        return caps;
    }

    caps.Ansi = (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
    caps.Utf8 = ::GetConsoleOutputCP() == CP_UTF8;

    // VT 能用就按真彩色报。老 conhost 只认 16 色时，越界的颜色会被它就近映射，
    // 不会崩；真在乎这一点的场景再接 Windows Terminal。
    caps.Depth = caps.Ansi ? ColorDepth::TrueColor : ColorDepth::None;

    caps.TermName = EnvValue("WT_SESSION")[0] != '\0' ? "windows-terminal"
                                                      : "windows-console";
    caps.Window   = GetSizePlatform(stream);
    return caps;
}

bool EnablePlatform(Stream stream)
{
    HANDLE handle = HandleOf(stream);
    DWORD  mode   = 0;
    if (!QueryMode(handle, mode))
        return false; // 重定向：没有模式可开，也没必要开

    if (stream == Stream::Err)
    {
        if (!g_saved.ErrSaved)
        {
            g_saved.ErrMode  = mode;
            g_saved.ErrSaved = true;
        }
    }
    else
    {
        if (!g_saved.OutSaved)
        {
            g_saved.OutMode  = mode;
            g_saved.OutSaved = true;
        }
    }

    const DWORD wanted = mode | ENABLE_PROCESSED_OUTPUT |
                         ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (wanted != mode)
        ::SetConsoleMode(handle, wanted);

    if (::GetConsoleOutputCP() != CP_UTF8)
    {
        if (!g_saved.CodePageSaved)
        {
            g_saved.OutputCodePage = ::GetConsoleOutputCP();
            g_saved.CodePageSaved  = true;
        }
        ::SetConsoleOutputCP(CP_UTF8);
    }

    DWORD current = 0;
    return QueryMode(handle, current) &&
           (current & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
}

void ShutdownPlatform()
{
    if (g_saved.OutSaved)
        ::SetConsoleMode(HandleOf(Stream::Out), g_saved.OutMode);

    if (g_saved.ErrSaved)
        ::SetConsoleMode(HandleOf(Stream::Err), g_saved.ErrMode);

    if (g_saved.CodePageSaved)
        ::SetConsoleOutputCP(g_saved.OutputCodePage);

    g_saved = SavedConsole{};
}

Size GetSizePlatform(Stream stream)
{
    Size size;

    HANDLE handle = HandleOf(stream);
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE &&
        ::GetConsoleScreenBufferInfo(handle, &info) != 0)
    {
        size.Columns = static_cast<std::uint16_t>(info.srWindow.Right - info.srWindow.Left + 1);
        size.Rows    = static_cast<std::uint16_t>(info.srWindow.Bottom - info.srWindow.Top + 1);
    }

    return size;
}

bool IsInteractivePlatform()
{
    DWORD mode = 0;
    if (!QueryMode(::GetStdHandle(STD_INPUT_HANDLE), mode))
        return false;

    return QueryMode(HandleOf(Stream::Out), mode);
}

} // namespace X_Y::Terminal::Detail
