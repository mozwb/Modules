#include <Terminal/Shell.h>
#include <Terminal/Terminal.h>

#include <string>
#include <vector>

namespace
{
    std::string Join(const std::vector<std::string> &items)
    {
        std::string line;
        for (const auto &item : items)
        {
            if (!line.empty())
                line += ' ';
            line += item;
        }
        return line;
    }
} // namespace

int main()
{
    using namespace X_Y;

    Terminal::Init();

    Command::Registry registry;

    registry.Add("echo", [](const Command::Args &args)
    {
        Terminal::Write(Join(args.Items) + "\n");
        return Command::Result{};
    });

    registry.Add("fail", [](const Command::Args &)
    {
        return Command::Result{3, true, "故意失败"};
    });

    registry.Add("clear", [](const Command::Args &)
    {
        if (!Terminal::Clear())
            Terminal::Write("(当前输出不是终端，clear 跳过)\n");
        return Command::Result{};
    });

    Shell::Shell shell(registry);
    shell.SetPrompt("xy> ");

    const int code = shell.Run();

    Terminal::Shutdown();
    return code;
}
