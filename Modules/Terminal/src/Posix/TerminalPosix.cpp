#include "../Platform.h"

#include <sys/ioctl.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>

namespace X_Y::Terminal::Detail
{
namespace
{
    int FdOf(Stream stream)
    {
        return stream == Stream::Err ? STDERR_FILENO : STDOUT_FILENO;
    }

    const char *EnvValue(const char *name)
    {
        const char *value = std::getenv(name);
        return value != nullptr ? value : "";
    }

    bool Contains(const char *text, const char *needle)
    {
        return std::strstr(text, needle) != nullptr;
    }
} // namespace

Caps ProbePlatform(Stream stream)
{
    Caps caps;

    const int fd = FdOf(stream);
    caps.Tty = ::isatty(fd) != 0;

    // v1 不做编码转换：调用方给的就是 UTF-8 字节，原样写。
    caps.Utf8 = true;

    const char *term = EnvValue("TERM");
    if (term[0] != '\0')
        caps.TermName = term;

    if (!caps.Tty || term[0] == '\0' || std::strcmp(term, "dumb") == 0)
        return caps;

    caps.Ansi = true;

    const char *colorterm = EnvValue("COLORTERM");
    if (Contains(colorterm, "truecolor") || Contains(colorterm, "24bit"))
        caps.Depth = ColorDepth::TrueColor;
    else if (Contains(term, "256color"))
        caps.Depth = ColorDepth::Ansi256;
    else
        caps.Depth = ColorDepth::Ansi16;

    caps.Window = GetSizePlatform(stream);
    return caps;
}

bool EnablePlatform(Stream stream)
{
    // POSIX 终端不需要“打开”什么；$TERM 里写了支持就是支持。
    return ProbePlatform(stream).Ansi;
}

void ShutdownPlatform()
{
}

Size GetSizePlatform(Stream stream)
{
    Size size;

    const int fd = FdOf(stream);
    if (::isatty(fd) == 0)
        return size;

    struct winsize window{};
    if (::ioctl(fd, TIOCGWINSZ, &window) == 0 && window.ws_col > 0 && window.ws_row > 0)
    {
        size.Columns = window.ws_col;
        size.Rows    = window.ws_row;
        return size;
    }

    // 有些环境（老 SSH / CI）拿不到 ioctl，但会导出 COLUMNS / LINES。
    const char *columns = EnvValue("COLUMNS");
    const char *rows    = EnvValue("LINES");
    if (columns[0] != '\0' && rows[0] != '\0')
    {
        const long width  = std::strtol(columns, nullptr, 10);
        const long height = std::strtol(rows, nullptr, 10);
        if (width > 0 && width <= 65535 && height > 0 && height <= 65535)
        {
            size.Columns = static_cast<std::uint16_t>(width);
            size.Rows    = static_cast<std::uint16_t>(height);
        }
    }

    return size;
}

bool IsInteractivePlatform()
{
    return ::isatty(STDIN_FILENO) != 0 && ::isatty(STDOUT_FILENO) != 0;
}

} // namespace X_Y::Terminal::Detail
