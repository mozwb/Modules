#pragma once

/*
Shell.h
version 0.0.0

最小交互壳：提示符 + 行编辑 + 分发。
读一行 -> Parse -> Shell 内建（help / exit / quit）或 Command::Registry。

交互终端里用 <Input/Console.h> 的 ReadKey 自己维护 buffer / 光标 / 历史；
stdin 被重定向时退回 std::getline，不打提示符，方便管道和脚本。

────────────────────────────────────────────────────────────────
做

  Run()           从 stdin 读行，直到 EOF / exit / quit；返回最后一条命令的退出码。
  Execute(line)   执行一行；空行返回 -1；exit / quit 会设置停止标志。
  SetPrompt()     提示符；只有 stdin + stdout 都是终端时才真的打印。
  Stop()          让 Run 在当前行结束后退出。

交互编辑

  左右 / Home / End / Backspace / Delete
  上下翻历史（最多 100 条，连续重复不记）
  Ctrl+A/E 行首行尾，Ctrl+U/K 删到行首/行尾，Ctrl+W 删前一个词
  Ctrl+C 取消当前行，Ctrl+D 空行退出，Ctrl+L 清屏重画

内建

  help            列出 Shell 内建 + Registry 里注册的命令。
  exit [code]     退出，默认 0；code 必须是 0-255。
  quit [code]     同上。

不做

  Tab 补全、多行编辑、输入法组合        -> 以后
  cd、管道、重定向                       -> 以后

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

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

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

        // 交互终端里读一行；返回 false 表示 EOF。
        bool ReadLineInteractive(std::string &line);
        void AddHistory(std::string line);
        void RedrawLine(const std::string &buffer, std::size_t cursor) const;

        Command::Registry &_registry;
        std::string        _prompt        = "> ";
        bool               _stopRequested = false;
        int                _lastExitCode  = 0;
        std::vector<std::string> _history;
    };
}
