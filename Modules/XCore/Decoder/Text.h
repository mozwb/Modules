#pragma once

#include "Decode.h"

#include <climits>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#ifdef XY_PLATFORM_WINDOWS
#include <windows.h>
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
