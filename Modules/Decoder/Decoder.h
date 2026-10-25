#pragma once

/*
Decoder.h
version 0.0.0

解码基础设施：通用解码器分派模板 + 文本编码转换。
这是 Decoder 模块唯一的公开头。

────────────────────────────────────────────────────────────────
内容
  DecoderBlueprint<Source>   解码蓝图接口（Decode(Source&)）
  Decoder                    分派器：接受 blueprint / lambda / 成员函数
                             Decode(src) / Decode(src, target) / DecodeNext(...)
  X_Y::Decode::TextEncoding  GBK / UTF8 / UTF16
  X_Y::Decode::Text<E>       编码标签类型 + 视图
  X_Y::Decode::ToGBK/ToUTF8/ToUTF16   编码互转（仅 Windows，走 Win32 API）

────────────────────────────────────────────────────────────────
用法

    #include <Decoder/Decoder.h>
    using namespace X_Y;

    // 1) 编码转换（仅 Windows）
    X_Y::Decode::ToUTF8 toUtf8;
    auto u8 = toUtf8.Decode(X_Y::Decode::GBKText(gbkString));
    if (u8) { ... }   // *u8 就是 std::string

    // 2) 通用解码：给 blueprint 或 lambda 都行
    auto value = Decoder::Decode<MyBlueprint>(source);
    Decoder::Decode(source, target);          // 就地解到 target
    Decoder::Decode(source, [](auto &s) { ... });

────────────────────────────────────────────────────────────────
⚠ 平台说明

  X_Y::Decode 下的转换工具依赖 <windows.h>（MultiByteToWideChar /
  WideCharToMultiByte），整块被 #ifdef XY_PLATFORM_WINDOWS 包住。
  这是目前唯一一个公开头里带 OS 头的模块 —— 将来把平台实现收进
  engine 平台层时，这里只留 TextEncoding / Text<E> 与抽象接口。

⚠ 定位说明

  Decoder 是过渡模块：图片/媒体蓝图（Blueprint/Image.h、Media.h）
  将来会并入 Media 模块。
────────────────────────────────────────────────────────────────
*/

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <Core/Core.h>
#include <Memory/Memory.h>
#include <Files/Files.h>

#ifdef XY_PLATFORM_WINDOWS
#include <windows.h>
#endif
namespace X_Y
{
    template <typename Source>
    class DecoderBlueprint
    {
    public:
        virtual ~DecoderBlueprint() = default;
        virtual void Decode(Source &source) = 0;
    };

    // 因为可能会有一堆解析格式，会有一堆调用方式，这里提供一个通用的接口，
    // 其实还是要实现解析方式和返回的数据格式，但是从调用上看都收敛于此，会比较统一吧。
    // 之后会写好一些常用的解析方式和数据结构组合，方便直接使用
    class Decoder
    {
    public:
        template <typename Blueprint, typename Source>
            requires std::is_default_constructible_v<Blueprint>
        static auto Decode(Source &&source)
        {
            Blueprint blueprint{};
            Decode(std::forward<Source>(source), blueprint);
            return blueprint;
        }

        template <typename Blueprint, typename Source, typename Target>
            requires std::is_default_constructible_v<Blueprint>
        static decltype(auto) Decode(Source &&source, Target &target)
        {
            return Decode(
                std::forward<Source>(source),
                target,
                Blueprint{});
        }

        template <typename Source, typename Blueprint>
        static decltype(auto) Decode(Source &&source, Blueprint &&blueprint)
        {
            if constexpr (requires {
                              std::forward<Blueprint>(blueprint).Decode(
                                  std::forward<Source>(source));
                          })
            {
                return std::forward<Blueprint>(blueprint).Decode(
                    std::forward<Source>(source));
            }
            else
            {
                return std::invoke(
                    std::forward<Blueprint>(blueprint),
                    std::forward<Source>(source));
            }
        }

        template <typename Source, typename Target, typename Blueprint>
        static decltype(auto) Decode(Source &&source, Target &target, Blueprint &&blueprint)
        {
            if constexpr (requires {
                              std::forward<Blueprint>(blueprint).Decode(
                                  std::forward<Source>(source), target);
                          })
            {
                return std::forward<Blueprint>(blueprint).Decode(
                    std::forward<Source>(source), target);
            }
            else if constexpr (std::is_invocable_v<Blueprint, Source, Target &>)
            {
                return std::invoke(
                    std::forward<Blueprint>(blueprint),
                    std::forward<Source>(source),
                    target);
            }
            else
            {
                target = Decode(
                    std::forward<Source>(source),
                    std::forward<Blueprint>(blueprint));
                return (target);
            }
        }

        template <typename Blueprint, typename Target>
        static decltype(auto) DecodeNext(Blueprint &blueprint, Target &target)
        {
            if constexpr (requires {
                              blueprint.DecodeNext(target);
                          })
            {
                return blueprint.DecodeNext(target);
            }
            else
            {
                return blueprint.ReadNext(target);
            }
        }
    };
}

#ifdef XY_PLATFORM_WINDOWS
#endif

namespace X_Y::Decode
{
    enum class TextEncoding
    {
        GBK,
        UTF8,
        UTF16
    };

    template <TextEncoding Encoding>
    class Text
    {
    public:
        using Character = std::conditional_t<Encoding == TextEncoding::UTF16, wchar_t, char>;
        using View = std::basic_string_view<Character>;
        using String = std::basic_string<Character>;

        Text() = default;
        Text(View text) : m_Text(text) {}
        Text(const String &text) : m_Text(text) {}
        Text(const Character *text) : m_Text(text ? View(text) : View{}) {}

        View ViewText() const { return m_Text; }

    private:
        View m_Text;
    };

    using GBKText = Text<TextEncoding::GBK>;
    using UTF8Text = Text<TextEncoding::UTF8>;
    using UTF16Text = Text<TextEncoding::UTF16>;

#ifdef XY_PLATFORM_WINDOWS
    namespace detail
    {
        template <TextEncoding Encoding>
        constexpr UINT CodePage = Encoding == TextEncoding::GBK ? 936 : CP_UTF8;

        inline std::optional<std::wstring> multibyte_to_wide(std::string_view input, UINT codePage)
        {
            if (input.empty())
                return std::wstring{};
            if (input.size() > static_cast<std::size_t>(INT_MAX))
                return std::nullopt;

            const int inputLength = static_cast<int>(input.size());
            const int outputLength = MultiByteToWideChar(codePage, 0, input.data(), inputLength, nullptr, 0);
            if (outputLength <= 0)
                return std::nullopt;

            std::wstring output(outputLength, L'\0');
            if (MultiByteToWideChar(codePage, 0, input.data(), inputLength, output.data(), outputLength) != outputLength)
                return std::nullopt;
            return output;
        }

        inline std::optional<std::string> wide_to_multibyte(std::wstring_view input, UINT codePage)
        {
            if (input.empty())
                return std::string{};
            if (input.size() > static_cast<std::size_t>(INT_MAX))
                return std::nullopt;

            const int inputLength = static_cast<int>(input.size());
            const int outputLength = WideCharToMultiByte(codePage, 0, input.data(), inputLength, nullptr, 0, nullptr, nullptr);
            if (outputLength <= 0)
                return std::nullopt;

            std::string output(outputLength, '\0');
            if (WideCharToMultiByte(codePage, 0, input.data(), inputLength, output.data(), outputLength, nullptr, nullptr) != outputLength)
                return std::nullopt;
            return output;
        }

        template <TextEncoding Encoding>
        std::optional<std::wstring> ToUTF16(Text<Encoding> input)
        {
            if constexpr (Encoding == TextEncoding::UTF16)
                return std::wstring(input.ViewText());
            else
                return multibyte_to_wide(input.ViewText(), CodePage<Encoding>);
        }

        template <TextEncoding Encoding>
        auto FromUTF16(std::wstring_view input) -> std::optional<typename Text<Encoding>::String>
        {
            if constexpr (Encoding == TextEncoding::UTF16)
                return std::wstring(input);
            else
                return wide_to_multibyte(input, CodePage<Encoding>);
        }
    }

    template <TextEncoding TargetEncoding>
    struct ToUTF
    {
        template <TextEncoding SourceEncoding>
        auto Decode(Text<SourceEncoding> input) const -> std::optional<typename Text<TargetEncoding>::String>
        {
            if constexpr (SourceEncoding == TargetEncoding)
                return typename Text<TargetEncoding>::String(input.ViewText());

            auto wide = detail::ToUTF16(input);
            if (!wide)
                return std::nullopt;
            return detail::FromUTF16<TargetEncoding>(*wide);
        }
    };

    using ToGBK = ToUTF<TextEncoding::GBK>;
    using ToUTF8 = ToUTF<TextEncoding::UTF8>;
    using ToUTF16 = ToUTF<TextEncoding::UTF16>;
#endif
}
