#pragma once

/*
Process.h
version 0.0.0

启动外部程序、等它结束、拿退出码。Command 查不到内建命令时用它兜底。

────────────────────────────────────────────────────────────────
做

  Run(program, args)   继承 stdin / stdout / stderr、环境变量、当前目录；
                       阻塞到子进程退出，返回退出码。
  Result               Started / ExitCode / Error，不抛异常。

不做

  捕获输出（管道）      -> 以后加 Capture
  shell 语法            -> | > && 通配符都不解释；要就 cmd /c 或 sh -c
  超时 / 取消           -> 以后
  环境变量注入 / 改 cwd -> 以后

平台

  Windows   CreateProcessW；参数从 UTF-8 转 UTF-16，命令行按 Windows 规则加引号。
  Linux     fork + execvp；用 CLOEXEC 错误管道，exec 失败能回报给父进程。

退出码

  正常退出        -> 子进程的退出码
  POSIX 被信号杀  -> 128 + signal
  启动失败        -> Started=false，ExitCode=-1，Error 里写原因

────────────────────────────────────────────────────────────────
用法

    #include <Terminal/Process.h>
    using namespace X_Y;

    const auto result = Process::Run("git", {"status"});
    if (!result.Started)
        Terminal::Write("[error] " + result.Error + "\n");
    else if (result.ExitCode != 0)
        Terminal::Write("[exit " + std::to_string(result.ExitCode) + "]\n");
────────────────────────────────────────────────────────────────
*/

#include <string>
#include <string_view>
#include <vector>

namespace X_Y::Process
{
    struct Result
    {
        bool        Started  = false;
        int         ExitCode = -1;
        std::string Error;

        bool Ok() const
        {
            return Started && ExitCode == 0 && Error.empty();
        }
    };

    // arguments 不含 program 本身。
    // program 可以带路径，也可以只是名字（走 PATH 查找）。
    Result Run(std::string_view program, const std::vector<std::string> &arguments);
}
