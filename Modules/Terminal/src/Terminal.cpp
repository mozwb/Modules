#include <Terminal/Terminal.h>

#include "Platform.h"

#include <cstdlib>
#include <iostream>
#include <mutex>

namespace X_Y::Terminal
{
namespace
{
    std::mutex g_initMutex;
    bool       g_initialized = false;
    Caps       g_caps[2];              // [0] = Out, [1] = Err
    int        g_forceColors = -1;     // -1 跟随环境，0 关，1 开

    Caps &CapsOf(Stream stream)
    {
        return g_caps[stream == Stream::Err ? 1 : 0];
    }

    std::ostream &OstreamOf(Stream stream)
    {
        return stream == Stream::Err ? std::cerr : std::cout;
    }
} // namespace

bool Init()
{
    std::lock_guard<std::mutex> lock(g_initMutex);
    if (g_initialized)
        return CapsOf(Stream::Out).Ansi;

    // 先开能力，再探测 —— 探测要看到开完之后的真实状态。
    Detail::EnablePlatform(Stream::Out);
    Detail::EnablePlatform(Stream::Err);

    CapsOf(Stream::Out) = Detail::ProbePlatform(Stream::Out);
    CapsOf(Stream::Err) = Detail::ProbePlatform(Stream::Err);
    g_initialized = true;

    return CapsOf(Stream::Out).Ansi;
}

void Shutdown()
{
    std::lock_guard<std::mutex> lock(g_initMutex);
    if (!g_initialized)
        return;

    Detail::ShutdownPlatform();
    CapsOf(Stream::Out) = Caps{};
    CapsOf(Stream::Err) = Caps{};
    g_initialized = false;
}

Caps Probe(Stream stream)
{
    return Detail::ProbePlatform(stream);
}

Caps GetCaps(Stream stream)
{
    Init();

    std::lock_guard<std::mutex> lock(g_initMutex);
    return CapsOf(stream);
}

bool IsInteractive()
{
    Init();
    return Detail::IsInteractivePlatform();
}

bool ColorsEnabled(Stream stream)
{
    const Caps caps = GetCaps(stream);
    if (caps.Depth == ColorDepth::None)
        return false;

    if (g_forceColors == 0)
        return false;
    if (g_forceColors == 1)
        return true;

    return std::getenv("NO_COLOR") == nullptr;
}

void SetColorsEnabled(bool on)
{
    g_forceColors = on ? 1 : 0;
}

std::string Paint(std::string_view text, const Style &style, Stream stream)
{
    if (!ColorsEnabled(stream))
        return std::string(text);

    return Paint(text, style, GetCaps(stream).Depth);
}

void Write(std::string_view text, Stream stream)
{
    if (text.empty())
        return;

    Init();

    std::ostream &os = OstreamOf(stream);
    os.write(text.data(), static_cast<std::streamsize>(text.size()));
    os.flush();
}

void Flush(Stream stream)
{
    OstreamOf(stream).flush();
}

Size GetSize(Stream stream)
{
    const Caps caps = GetCaps(stream);

    Size size = Detail::GetSizePlatform(stream);
    if (size.Columns == 0 || size.Rows == 0)
        size = caps.Window;

    return size;
}

} // namespace X_Y::Terminal
