#include <Terminal/Terminal.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace X_Y::Terminal
{
namespace
{
    // xterm 的 16 色板。做最近色时用，不保证和用户主题完全一致，
    // 但比“随便取一个”稳。
    constexpr Rgb kPalette16[16] = {
        {0, 0, 0},       {128, 0, 0},     {0, 128, 0},     {128, 128, 0},
        {0, 0, 128},     {128, 0, 128},   {0, 128, 128},   {192, 192, 192},
        {128, 128, 128}, {255, 0, 0},     {0, 255, 0},     {255, 255, 0},
        {0, 0, 255},     {255, 0, 255},   {0, 255, 255},   {255, 255, 255}};

    // 256 色立方体的 6 个灰度台阶（xterm 的真实值，不是 0/51/102…）
    constexpr std::uint8_t kCubeLevels[6] = {0, 95, 135, 175, 215, 255};

    int DistanceSquared(const Rgb &a, const Rgb &b)
    {
        const int dr = static_cast<int>(a.R) - static_cast<int>(b.R);
        const int dg = static_cast<int>(a.G) - static_cast<int>(b.G);
        const int db = static_cast<int>(a.B) - static_cast<int>(b.B);
        return dr * dr + dg * dg + db * db;
    }

    int NearestPalette16(const Rgb &color)
    {
        int best = 0;
        int bestDistance = 255 * 255 * 3;

        for (int i = 0; i < 16; ++i)
        {
            const int distance = DistanceSquared(color, kPalette16[i]);
            // 距离相等时取后面的（亮色）——橙色 (255,128,0) 到黄 / 亮黄是平局，
            // 亮黄更接近肉眼看到的意思。
            if (distance <= bestDistance)
            {
                bestDistance = distance;
                best = i;
            }
        }
        return best;
    }

    int NearestCubeLevel(std::uint8_t value)
    {
        int best = 0;
        int bestDistance = 255 * 255;

        for (int i = 0; i < 6; ++i)
        {
            const int diff = static_cast<int>(value) - static_cast<int>(kCubeLevels[i]);
            const int distance = diff * diff;
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = i;
            }
        }
        return best;
    }

    std::uint8_t Nearest256(const Rgb &color)
    {
        const int r = NearestCubeLevel(color.R);
        const int g = NearestCubeLevel(color.G);
        const int b = NearestCubeLevel(color.B);

        const Rgb cube{kCubeLevels[r], kCubeLevels[g], kCubeLevels[b]};
        const int cubeIndex = 16 + 36 * r + 6 * g + b;
        const int cubeDistance = DistanceSquared(color, cube);

        // 灰阶 232..255 对应 8,18,...,238
        const int average = (static_cast<int>(color.R) + color.G + color.B) / 3;
        int grayIndex = 0;
        if (average < 8)
            grayIndex = 0;
        else if (average > 238)
            grayIndex = 23;
        else
            grayIndex = (average - 8) / 10;

        const std::uint8_t level = static_cast<std::uint8_t>(8 + grayIndex * 10);
        const Rgb gray{level, level, level};
        const int grayDistance = DistanceSquared(color, gray);

        if (grayDistance < cubeDistance)
            return static_cast<std::uint8_t>(232 + grayIndex);
        return static_cast<std::uint8_t>(cubeIndex);
    }

    // Default 在最前，Black 开始连续 0..15；返回值 -1 表示 Default。
    int ColorIndex(Color color)
    {
        return static_cast<int>(color) - static_cast<int>(Color::Black);
    }

    int CodeFromIndex(int index, bool background)
    {
        if (index < 0)
            return background ? 49 : 39;
        if (index < 8)
            return (background ? 40 : 30) + index;
        return (background ? 100 : 90) + (index - 8);
    }

    int BasicCode(Color color, bool background)
    {
        return CodeFromIndex(ColorIndex(color), background);
    }

    void AppendCode(std::string &codes, std::string_view code)
    {
        if (!codes.empty())
            codes += ';';
        codes += code;
    }
} // namespace

std::string Sgr(const Style &style, ColorDepth depth)
{
    if (depth == ColorDepth::None)
        return {};

    std::string codes;

    if (style.Bold)
        AppendCode(codes, "1");
    if (style.Dim)
        AppendCode(codes, "2");
    if (style.Italic)
        AppendCode(codes, "3");
    if (style.Underline)
        AppendCode(codes, "4");
    if (style.Reverse)
        AppendCode(codes, "7");
    if (style.Strike)
        AppendCode(codes, "9");

    if (style.ForegroundRgb)
    {
        const Rgb &color = *style.ForegroundRgb;
        switch (depth)
        {
        case ColorDepth::TrueColor:
            AppendCode(codes, "38;2;" + std::to_string(color.R) + ";" +
                                  std::to_string(color.G) + ";" +
                                  std::to_string(color.B));
            break;
        case ColorDepth::Ansi256:
            AppendCode(codes, "38;5;" + std::to_string(Nearest256(color)));
            break;
        case ColorDepth::Ansi16:
            AppendCode(codes, std::to_string(CodeFromIndex(NearestPalette16(color), false)));
            break;
        case ColorDepth::None:
            break;
        }
    }
    else if (style.Foreground)
    {
        AppendCode(codes, std::to_string(BasicCode(*style.Foreground, false)));
    }

    if (style.BackgroundRgb)
    {
        const Rgb &color = *style.BackgroundRgb;
        switch (depth)
        {
        case ColorDepth::TrueColor:
            AppendCode(codes, "48;2;" + std::to_string(color.R) + ";" +
                                  std::to_string(color.G) + ";" +
                                  std::to_string(color.B));
            break;
        case ColorDepth::Ansi256:
            AppendCode(codes, "48;5;" + std::to_string(Nearest256(color)));
            break;
        case ColorDepth::Ansi16:
            AppendCode(codes, std::to_string(CodeFromIndex(NearestPalette16(color), true)));
            break;
        case ColorDepth::None:
            break;
        }
    }
    else if (style.Background)
    {
        AppendCode(codes, std::to_string(BasicCode(*style.Background, true)));
    }

    if (codes.empty())
        return {};

    std::string result;
    result.reserve(codes.size() + 3);
    result += "\x1b[";
    result += codes;
    result += 'm';
    return result;
}

std::string Paint(std::string_view text, const Style &style, ColorDepth depth)
{
    const std::string sgr = Sgr(style, depth);
    if (sgr.empty())
        return std::string(text);

    std::string result;
    result.reserve(sgr.size() + text.size() + 4);
    result += sgr;
    result += text;
    result += "\x1b[0m";
    return result;
}

std::string StripAnsi(std::string_view text)
{
    std::string result;
    result.reserve(text.size());

    std::size_t i = 0;
    while (i < text.size())
    {
        const unsigned char current = static_cast<unsigned char>(text[i]);

        if (current == 0x1B && i + 1 < text.size())
        {
            const unsigned char next = static_cast<unsigned char>(text[i + 1]);

            if (next == '[') // CSI: ESC [ 参数 终止符
            {
                i += 2;
                while (i < text.size())
                {
                    const unsigned char c = static_cast<unsigned char>(text[i]);
                    ++i;
                    if (c >= 0x40 && c <= 0x7E) // final byte
                        break;
                }
                continue;
            }

            if (next == ']') // OSC: ESC ] ... BEL，或 ST（ESC + 反斜杠）
            {
                i += 2;
                while (i < text.size())
                {
                    if (text[i] == 0x07)
                    {
                        ++i;
                        break;
                    }
                    if (text[i] == 0x1B && i + 1 < text.size() && text[i + 1] == '\\')
                    {
                        i += 2;
                        break;
                    }
                    ++i;
                }
                continue;
            }

            i += 2; // 其他两字节 escape
            continue;
        }

        result += static_cast<char>(current);
        ++i;
    }

    return result;
}

} // namespace X_Y::Terminal
