#include "Core/Core.h"

namespace X_Y::Utils
{
    bool starts_with(const std::string &s, const std::string &prefix)
    {
        return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
    }
    bool ends_with(const std::string &s, const std::string &suffix)
    {
        if (s.size() < suffix.size())
            return false;
        return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    }
    bool contains(const std::string &s, const std::string &substr)
    {
        return s.find(substr) != std::string::npos;
    }

    bool IsSpace(char c)
    {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
    }
    std::string ToLower(const std::string &s)
    {
        std::string result = s;
        for (char &c : result)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return result;
    }

    std::string ToUpper(const std::string &s)
    {
        std::string result = s;
        for (char &c : result)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return result;
    }
    // 忽略大小写判断是否相等
    bool equals_ignore_case(const std::string &s1, const std::string &s2)
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

    bool IsNumber(const std::string &s)
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
    std::string TrimLeft(const std::string &s)
    {
        size_t start = 0;
        while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
            ++start;
        return s.substr(start);
    }
    std::string TrimRight(const std::string &s)
    {
        size_t end = s.size();
        while (end > 0 && std::isspace(static_cast<unsigned char>(s[end - 1])))
            --end;
        return s.substr(0, end);
    }
    std::string Trim(const std::string &s)
    {
        return TrimRight(TrimLeft(s));
    }
    std::vector<std::string> Split(const std::string &s, const std::string &delimiter)
    {
        std::vector<std::string> result;
        if (delimiter.empty())
        {
            result.push_back(s);
            return result;
        }

        size_t start = 0;
        size_t end = s.find(delimiter);
        while (end != std::string::npos)
        {
            result.push_back(s.substr(start, end - start));
            start = end + delimiter.length();
            end = s.find(delimiter, start);
        }
        result.push_back(s.substr(start)); // 最后一段（原实现漏掉了）
        return result;
    }
    std::string Replace(const std::string &s, const std::string &old_value, const std::string &new_value)
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
    bool IsBlank(const std::string &s)
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
    bool ParseUnsigned(std::string_view text, std::uint32_t &value)
    {
        if (text.empty())
            return false;

        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    }
} // namespace X_Y::Utils
