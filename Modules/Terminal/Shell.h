#pragma once

/*
Shell.h
version 0.0.0

最小交互壳：提示符 + std::getline + 分发。
读一行 -> Parse -> Shell 内建（help / exit / quit）或 Command::Registry。

────────────────────────────────────────────────────────────────
做

  Run()           从 stdin 读行，直到 EOF / exit / quit；返回最后一条命令的退出码。
  Execute(line)   执行一行；空行返回 -1；exit / quit 会设置停止标志。
  SetPrompt()     提示符；只有 stdout 是 tty 时才真的打印。
  Stop()          让 Run 在当前行结束后退出。

内建

  help            列出 Shell 内建 + Registry 里注册的命令。
  exit [code]     退出，默认 0；code 必须是 0-255。
  quit [code]     同上。

不做

  历史、方向键、Tab 补全、Ctrl+C  -> 等 Terminal 的 ReadKey 那一版
  cd、管道、重定向                 -> 以后
  raw mode / 行编辑                -> 现在用 std::getline，交给终端自己回显

────────────────────────────────────────────────────────────────
用法

    #include <Terminal/Shell.h>
    #include <Terminal/Terminal.h>

    X_Y::Command::Registry commands;
    commands.Add("echo", ...);

    X_Y::Shell::Shell shell(commands);
    shell.SetPrompt("xy> ");
    return shell.Run();
────────────────────────────────────────────────────────────────
*/

#include <Terminal/Command.h>

#include <string>
#include <string_view>

namespace X_Y::Shell
{
    class Shell
    {
    public:
        explicit Shell(Command::Registry &registry);

        void        SetPrompt(std::string prompt);
        std::string GetPrompt() const;

        // 执行一行。空行返回 -1；解析失败返回 2；exit / quit 会设置停止标志。
        int Execute(std::string_view line);

        // 从 stdin 读行，直到 EOF / exit / quit。返回最后一条命令的退出码。
        int Run();

        // 让 Run 在当前命令结束后退出（给别的线程或 handler 用）。
        void Stop();

    private:
        int  ExecuteExit(const Command::Args &args);
        void PrintHelp();
        void PrintError(std::string_view message);

        Command::Registry &_registry;
        std::string        _prompt        = "> ";
        bool               _stopRequested = false;
        int                _lastExitCode  = 0;
    };
}
