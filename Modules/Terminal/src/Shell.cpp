#include <Terminal/Shell.h>
#include <Terminal/Terminal.h>

#include <Core/Core.h>

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace X_Y::Shell
{
Shell::Shell(Command::Registry &registry)
    : _registry(registry)
{
}

void Shell::SetPrompt(std::string prompt)
{
    _prompt = std::move(prompt);
}

std::string Shell::GetPrompt() const
{
    return _prompt;
}

void Shell::Stop()
{
    _stopRequested = true;
}

int Shell::Run()
{
    _stopRequested = false;
    _lastExitCode  = 0;

    const bool interactive = Terminal::IsInteractive();

    std::string line;
    while (!_stopRequested)
    {
        if (interactive)
            Terminal::Write(_prompt);

        if (!std::getline(std::cin, line))
        {
            if (interactive)
                Terminal::Write("\n");
            break;
        }

        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        const int code = Execute(line);
        if (code >= 0)
            _lastExitCode = code;
    }

    return _lastExitCode;
}

int Shell::Execute(std::string_view line)
{
    if (Utils::IsBlank(std::string(line)))
        return -1;

    Command::Args args;
    std::string   error;
    if (!Command::Parse(line, args, &error))
    {
        PrintError(error);
        return 2;
    }

    const std::string name = Utils::ToLower(args.Name);

    if (name == "exit" || name == "quit")
        return ExecuteExit(args);

    if (name == "help")
    {
        PrintHelp();
        return 0;
    }

    const Command::Result result = _registry.Run(args);
    if (!result.Error.empty())
        PrintError(result.Error);

    return result.ExitCode;
}

int Shell::ExecuteExit(const Command::Args &args)
{
    if (args.Items.size() > 1)
    {
        PrintError("exit / quit 只接受一个退出码");
        return 2;
    }

    int code = 0;
    if (!args.Items.empty())
    {
        const std::string &text = args.Items.front();

        char *end = nullptr;
        errno     = 0;
        const long value = std::strtol(text.c_str(), &end, 10);

        if (errno != 0 || end == text.c_str() || *end != '\0' ||
            value < 0 || value > 255)
        {
            PrintError("退出码必须是 0-255 的整数");
            return 2;
        }

        code = static_cast<int>(value);
    }

    _stopRequested = true;
    return code;
}

void Shell::PrintHelp()
{
    const std::vector<std::string> names = _registry.Names();

    std::string line = "commands:";
    if (names.empty())
    {
        line += " (none)";
    }
    else
    {
        for (const auto &name : names)
            line += " " + name;
    }

    Terminal::Write("built-in: help, exit [code], quit [code]\n");
    Terminal::Write(line + "\n");
}

void Shell::PrintError(std::string_view message)
{
    const Terminal::Style style =
        Terminal::Style{}.SetFg(Terminal::Color::BrightRed).SetBold();

    Terminal::Write(Terminal::Paint("[error] " + std::string(message), style) + "\n");
}

} // namespace X_Y::Shell
