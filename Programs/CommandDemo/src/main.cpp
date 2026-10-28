#include <Terminal/Command.h>
#include <Terminal/Terminal.h>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    int Check(bool ok, const char *name)
    {
        X_Y::Terminal::Write(std::string(name) + (ok ? "  ok\n" : "  FAIL\n"));
        return ok ? 0 : 1;
    }

    void Show(X_Y::Command::Registry &registry, std::string_view line)
    {
        X_Y::Terminal::Write("> " + std::string(line) + "\n");

        const auto result = registry.Run(line);
        if (!result.Error.empty())
            X_Y::Terminal::Write("[error] " + result.Error + "\n");

        X_Y::Terminal::Write("[exit " + std::to_string(result.ExitCode) + "]\n");
    }
} // namespace

int main()
{
    using namespace X_Y;

    Terminal::Init();

    Command::Registry         registry;
    std::vector<std::string>  captured;

    registry.Add("echo", [](const Command::Args &args)
    {
        std::string line;
        for (const auto &item : args.Items)
        {
            if (!line.empty())
                line += ' ';
            line += item;
        }
        Terminal::Write(line + "\n");
        return Command::Result{};
    });

    registry.Add("capture", [&captured](const Command::Args &args)
    {
        captured = args.Items;
        return Command::Result{};
    });

    registry.Add("help", [&registry](const Command::Args &)
    {
        std::string line = "commands:";
        for (const auto &name : registry.Names())
            line += " " + name;
        Terminal::Write(line + "\n");
        return Command::Result{};
    });

    registry.Add("fail", [](const Command::Args &)
    {
        return Command::Result{3, true, "故意的失败"};
    });

    int failures = 0;

    const auto echo = registry.Run("echo hello world");
    failures += Check(echo.Ok(), "echo exit 0");

    const auto quoted = registry.Run("capture \"hello world\" 'x y' plain");
    failures += Check(quoted.Ok() && captured.size() == 3 &&
                          captured[0] == "hello world" &&
                          captured[1] == "x y" &&
                          captured[2] == "plain",
                      "quotes");

    const auto escaped = registry.Run("capture \"a\\\"b\" c\\ d");
    failures += Check(escaped.Ok() && captured.size() == 2 &&
                          captured[0] == "a\"b" &&
                          captured[1] == "c d",
                      "escapes");

    const auto emptyItem = registry.Run("capture \"\"");
    failures += Check(emptyItem.Ok() && captured.size() == 1 && captured[0].empty(),
                      "empty token");

    const auto joined = registry.Run("capture pre\"mid dle\"post");
    failures += Check(joined.Ok() && captured.size() == 1 &&
                          captured[0] == "premid dlepost",
                      "adjacent join");

    const auto unknown = registry.Run("No-Such-Command");
    failures += Check(!unknown.Started && unknown.ExitCode == 127, "unknown 127");

    const auto upper = registry.Run("ECHO hi");
    failures += Check(upper.Ok(), "case insensitive");

    const auto badQuote = registry.Run("echo \"unterminated");
    failures += Check(!badQuote.Started && badQuote.ExitCode == 2, "bad quote 2");

    const auto trailing = registry.Run("echo oops\\");
    failures += Check(!trailing.Started && trailing.ExitCode == 2, "trailing escape 2");

    const auto blank = registry.Run("   ");
    failures += Check(!blank.Started && blank.ExitCode == 2, "blank 2");

    const auto failed = registry.Run("fail");
    failures += Check(failed.Started && failed.ExitCode == 3 && !failed.Ok(), "handler 3");

#if defined(XY_PLATFORM_WINDOWS)
    const std::string exitLine = "cmd.exe /c exit 7";
    const std::string echoLine = "cmd.exe /c echo external works";
#else
    const std::string exitLine = "/bin/sh -c \"exit 7\"";
    const std::string echoLine = "/bin/sh -c \"echo external works\"";
#endif

    const auto external = registry.Run(exitLine);
    failures += Check(external.Started && external.ExitCode == 7, "external exit 7");

    const auto missing = registry.Run("no-such-program-xyz-123");
    failures += Check(!missing.Started && missing.ExitCode == 127, "external missing 127");

    registry.SetExternalEnabled(false);
    const auto disabled = registry.Run(exitLine);
    failures += Check(!disabled.Started && disabled.ExitCode == 127, "external off");
    registry.SetExternalEnabled(true);

    registry.Remove("capture");
    failures += Check(!registry.Has("capture"), "remove");

    Terminal::Write("\n--- demo ---\n");
    Show(registry, "help");
    Show(registry, "echo \"hello world\" 'x y'");
    Show(registry, "echo \"unterminated");
    Show(registry, echoLine);
    Show(registry, "No-Such-Command");
    Show(registry, "fail");

    Terminal::Shutdown();

    std::printf("failures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
