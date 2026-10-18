#pragma once
#include <Log/XYLog.h>
#include "Memory/Buffer.h"
#include "Timer/Timer.h"
#include <string>

namespace X_Y
{

	// @@ ProfileScope：作用域性能分析 RAII 类，析构时自动打印函数名 + 耗时
	// @@ 基础用法：XY_PROFILE_FUNCTION()
	// @@ 追加额外信息：XY_PROFILE_FUNCTION() << "count=100" << "batch=32"
	struct ProfileScope
	{
		StopWatch timer;
		std::string funcName;
		std::string extraInfo;

		ProfileScope(const char *func) : funcName(func) {}

		~ProfileScope()
		{
			if (extraInfo.empty())
				XPINK("[{}] 耗时 {:.2f}ms", funcName, timer.Milliseconds())
			else
				XPINK("[{}] 耗时 {:.2f}ms | {}", funcName, timer.Milliseconds(), extraInfo)
		}

		// @@ 用 << 链式追加额外信息，输出格式："[func] 耗时 xx.xxms | info1 | info2 | ..."
		ProfileScope &operator<<(const std::string &info)
		{
			if (!extraInfo.empty())
				extraInfo += " | ";
			extraInfo += info;
			return *this;
		}
	};

}

// @@ 宏辅助：拼接 token
#define XY_PASTE2(a, b) a##b
#define XY_PASTE(a, b) XY_PASTE2(a, b)

#ifdef ENABLEPROFILE
// @@ 创建 ProfileScope 变量，变量名 = prof_ + __LINE__，防止嵌套冲突
// @@ 示例：
//   void Foo() { XY_PROFILE_FUNCTION(); ... }  // 自动打印 [Foo] 耗时 3.45ms
//   void Bar(int n) {
//       XY_PROFILE_FUNCTION() << "n=" + std::to_string(n);
//       ...  // 自动打印 [Bar] 耗时 5.12ms | n=42
//   }
#define XY_PROFILE_FUNCTION() \
	X_Y::ProfileScope XY_PASTE(prof_, __LINE__)(__FUNCTION__);
#else
#define XY_PROFILE_FUNCTION()
#endif

namespace X_Y
{
	inline bool starts_with(const std::string &s, const std::string &prefix)
	{
		return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
	}
	inline bool ends_with(const std::string &s, const std::string &suffix)
	{
		if (s.size() < suffix.size())
			return false;
		return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
	}
	inline bool contains(const std::string &s, const std::string &substr)
	{
		return s.find(substr) != std::string::npos;
	}
	inline std::string ToLower(const std::string &s)
	{
		std::string result = s;
		for (char &c : result)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return result;
	}

	inline std::string ToUpper(const std::string &s)
	{
		std::string result = s;
		for (char &c : result)
			c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		return result;
	}
	// 忽略大小写判断是否相等
	inline bool equals_ignore_case(const std::string &s1, const std::string &s2)
	{
		if (s1.size() != s2.size())
			return false;
		for (size_t i = 0; i < s1.size(); ++i)
		{
			auto c1 = static_cast<unsigned char>(s1[i]);
			auto c2 = static_cast<unsigned char>(s2[i]);
			if (std::tolower(c1) != std::tolower(c2))
				return false;
		}
		return true;
	}

	inline bool IsNumber(const std::string &s)
	{
		if (s.empty())
			return false;
		for (char c : s)
		{
			if (!std::isdigit(static_cast<unsigned char>(c)))
				return false;
		}
		return true;
	}
	inline std::string Trim(const std::string &s)
	{
		return TrimRight(TrimLeft(s));
	}
	inline std::string TrimLeft(const std::string &s)
	{
		size_t start = 0;
		while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
			++start;
		return s.substr(start);
	}
	inline std::string TrimRight(const std::string &s)
	{
		size_t end = s.size();
		while (end > 0 && std::isspace(static_cast<unsigned char>(s[end - 1])))
			--end;
		return s.substr(0, end);
	}
	inline std::vector<std::string> Split(const std::string &s, const std::string &delimiter)
	{
		std::vector<std::string> result;
		size_t start = 0;
		size_t end = s.find(delimiter);
		while (end != std::string::npos)
		{
			result.push_back(s.substr(start, end - start));
			start = end + delimiter.length();
			end = s.find(delimiter, start);
		}
		return result;
	}
	inline std::string Replace(const std::string &s, const std::string &old_value, const std::string &new_value)
	{
		std::string result = s;
		size_t pos = 0;
		while ((pos = result.find(old_value, pos)) != std::string::npos)
		{
			result.replace(pos, old_value.length(), new_value);
			pos += new_value.length();
		}
		return result;
	}
	inline bool IsBlank(const std::string &s)
	{
		for (char c : s)
		{
			if (!std::isspace(static_cast<unsigned char>(c)))
			{
				return false;
			}
		}
		return true;
	}
	inline bool ParseUnsigned(std::string_view text, std::uint32_t &value)
	{
		if (text.empty())
			return false;

		const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
		return result.ec == std::errc{} && result.ptr == text.data() + text.size();
	}
}