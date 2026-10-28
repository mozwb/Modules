#include <Terminal/Command.h>
#include <Terminal/Process.h>

#include <Core/Core.h>

#include <cstddef>
#include <utility>

namespace X_Y::Command
{
    namespace
    {
        bool IsSpace(char c)
        {
            return Utils::IsSpace(c);
        }

        std::string Normalize(std::string_view name)
        {
            return Utils::ToLower(std::string(name));
        }
    } // namespace

    bool Parse(std::string_view line, Args &out, std::string *error)
    {
        auto fail = [&](std::string message)
        {
            if (error != nullptr)
                *error = std::move(message);
            out = Args{};
            return false;
        };

        std::vector<std::string> tokens;
        std::string token;
        bool hasToken = false;

        auto finishToken = [&]()
        {
            if (!hasToken)
                return;
            tokens.push_back(std::move(token));
            token.clear();
            hasToken = false;
        };

        std::size_t i = 0;
        while (i < line.size())
        {
            const char c = line[i];

            if (IsSpace(c))
            {
                finishToken();
                ++i;
                continue;
            }

            if (c == '\'') // 单引号：原样，不转义
            {
                hasToken = true;
                ++i;
                const std::size_t start = i;
                while (i < line.size() && line[i] != '\'')
                    ++i;
                if (i >= line.size())
                    return fail("单引号没有闭合");

                token.append(line.substr(start, i - start));
                ++i;
                continue;
            }

            if (c == '"') // 双引号：只认转义的引号和反斜杠
            {
                hasToken = true;
                ++i;
                bool closed = false;

                while (i < line.size())
                {
                    const char q = line[i];
                    if (q == '"')
                    {
                        ++i;
                        closed = true;
                        break;
                    }

                    if (q == '\\' && i + 1 < line.size())
                    {
                        const char escaped = line[i + 1];
                        if (escaped == '"' || escaped == '\\')
                        {
                            token += escaped;
                            i += 2;
                            continue;
                        }
                    }

                    token += q;
                    ++i;
                }

                if (!closed)
                    return fail("双引号没有闭合");
                continue;
            }

            if (c == '\\') // 引号外：转义下一个字符
            {
                if (i + 1 >= line.size())
                    return fail("转义符后面没有字符");

                hasToken = true;
                token += line[i + 1];
                i += 2;
                continue;
            }

            hasToken = true;
            token += c;
            ++i;
        }

        finishToken();

        if (tokens.empty())
            return fail("空命令");
        if (tokens.front().empty())
            return fail("命令名为空");

        out.Name = std::move(tokens.front());
        out.Items = std::vector<std::string>(tokens.begin() + 1, tokens.end());

        if (error != nullptr)
            error->clear();
        return true;
    }

    bool Registry::Add(std::string name, Handler handler)
    {
        if (name.empty() || !handler)
            return false;

        _handlers[Normalize(name)] = std::move(handler);
        return true;
    }

    bool Registry::Remove(std::string_view name)
    {
        return _handlers.erase(Normalize(name)) > 0;
    }

    bool Registry::Has(std::string_view name) const
    {
        return _handlers.find(Normalize(name)) != _handlers.end();
    }

    std::vector<std::string> Registry::Names() const
    {
        std::vector<std::string> names;
        names.reserve(_handlers.size());

        for (const auto &entry : _handlers)
            names.push_back(entry.first);

        return names;
    }

    void Registry::SetExternalEnabled(bool on)
    {
        _external = on;
    }

    bool Registry::ExternalEnabled() const
    {
        return _external;
    }

    Result Registry::Run(const Args &args)
    {
        if (args.Name.empty())
            return Result{2, false, "空命令"};

        const auto it = _handlers.find(Normalize(args.Name));
        if (it == _handlers.end())
        {
            if (!_external)
                return Result{127, false, "未知命令: " + args.Name};

            const Process::Result process = Process::Run(args.Name, args.Items);

            Result result;
            result.Started = process.Started;
            result.ExitCode = process.Started ? process.ExitCode : 127;
            result.Error = process.Error;

            if (!result.Started && result.Error.empty())
                result.Error = "未知命令: " + args.Name;

            return result;
        }

        return it->second(args);
    }

    Result Registry::Run(std::string_view line)
    {
        Args args;
        std::string error;

        if (!Parse(line, args, &error))
            return Result{2, false, std::move(error)};

        return Run(args);
    }

} // namespace X_Y::Command
