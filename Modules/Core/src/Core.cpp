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
    std::string TrimSpace(const std::string &s)
    {
        std::string res;
        for (char c : s)
        {
            if (c != ' ')
            {
                res += c;
            }
        }
        return res;
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

namespace X_Y
{
    GetoptParser::GetoptParser(int argc, char *const argv[],
                               std::string_view optionSpec)
        : argc_(argc), argv_(argv), index_(argc > 0 ? 1 : 0),
          optionSpec_(optionSpec)
    {
        if (argc < 0 || (argc > 0 && argv == nullptr))
        {
            Fail("argc 或 argv 无效");
            return;
        }

        for (std::size_t i = 0; i < optionSpec_.size(); ++i)
        {
            const char option = optionSpec_[i];
            if (option == ':' ||
                optionSpec_.find(option, i + 1) != std::string::npos)
            {
                Fail("选项规格包含无效或重复的选项");
                return;
            }
            if (i + 1 < optionSpec_.size() && optionSpec_[i + 1] == ':')
            {
                if (i + 2 < optionSpec_.size() && optionSpec_[i + 2] == ':')
                {
                    Fail("不支持可选值选项");
                    return;
                }
                ++i;
            }
        }
    }

    GetoptParser::Result GetoptParser::Fail(std::string message)
    {
        error_ = std::move(message);
        finished_ = true;
        return Result::Error;
    }

    GetoptParser::Result GetoptParser::Next()
    {
        option_ = '\0';
        optionValue_ = nullptr;
        argument_ = nullptr;

        if (!error_.empty())
        {
            return Result::Error;
        }
        if (finished_)
        {
            return Result::End;
        }

        for (;;)
        {
            if (pendingOptions_ != nullptr && *pendingOptions_ != '\0')
            {
                option_ = *pendingOptions_++;
                const std::size_t optionPosition = optionSpec_.find(option_);
                if (optionPosition == std::string::npos)
                {
                    return Fail(std::string("未知选项: -") + option_);
                }

                const bool requiresValue =
                    optionPosition + 1 < optionSpec_.size() &&
                    optionSpec_[optionPosition + 1] == ':';
                if (requiresValue)
                {
                    if (*pendingOptions_ != '\0')
                    {
                        optionValue_ = pendingOptions_;
                        pendingOptions_ = nullptr;
                    }
                    else
                    {
                        pendingOptions_ = nullptr;
                        if (index_ >= argc_)
                        {
                            return Fail(std::string("选项缺少值: -") + option_);
                        }
                        if (argv_[index_] == nullptr)
                        {
                            return Fail("argv 中包含空指针");
                        }
                        optionValue_ = argv_[index_++];
                    }
                }
                else if (*pendingOptions_ == '\0')
                {
                    pendingOptions_ = nullptr;
                }
                return Result::Option;
            }

            pendingOptions_ = nullptr;
            if (index_ >= argc_)
            {
                finished_ = true;
                return Result::End;
            }
            if (argv_[index_] == nullptr)
            {
                return Fail("argv 中包含空指针");
            }

            const char *arg = argv_[index_++];
            if (optionsEnabled_ && std::string_view(arg) == "--")
            {
                optionsEnabled_ = false;
                continue;
            }
            if (optionsEnabled_ && arg[0] == '-' && arg[1] != '\0')
            {
                if (arg[1] == '-')
                {
                    return Fail(std::string("不支持长选项: ") + arg);
                }
                pendingOptions_ = arg + 1;
                continue;
            }

            argument_ = arg;
            return Result::Argument;
        }
    }

    char GetoptParser::Option() const
    {
        return option_;
    }

    const char *GetoptParser::OptionValue() const
    {
        return optionValue_;
    }

    const char *GetoptParser::Argument() const
    {
        return argument_;
    }

    const char *GetoptParser::Error() const
    {
        return error_.empty() ? nullptr : error_.c_str();
    }

} // namespace X_Y
