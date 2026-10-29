#pragma once

/*
Process.h
version 0.0.0

启动外部程序、等它结束、拿退出码。Command 查不到内建命令时用它兜底。

────────────────────────────────────────────────────────────────
做

  Run(program, args, options)
                       默认继承 stdin / stdout / stderr、环境变量、当前目录；
                       阻塞到子进程退出，返回退出码。
  Options              CaptureStdout / CaptureStderr 打开时建 pipe 捕获对应流，
                       子进程输出不再进终端；两路并发读，避免 pipe 缓冲满死锁。
  Result               Started / ExitCode / Error / StdOut / StdErr，不抛异常。

不做

  shell 语法            -> | > && 通配符都不解释；要就 cmd /c 或 sh -c
  超时 / 取消           -> 以后
  环境变量注入 / 改 cwd -> 以后
  捕获时流式回调        -> 现在先全部读进内存，以后要的话再加逐块回调

平台

  Windows   CreateProcessW；参数从 UTF-8 转 UTF-16，命令行按 Windows 规则加引号。
            捕获时用 CreatePipe，读端不可继承，写端交给 STARTF_USESTDHANDLES。
  Linux     fork + execvp；用 CLOEXEC 错误管道，exec 失败能回报给父进程。
            捕获时 pipe + dup2 到 STDOUT/STDERR，父进程并发读。

退出码

  正常退出        -> 子进程的退出码
  POSIX 被信号杀  -> 128 + signal
  启动失败        -> Started=false，ExitCode=-1，Error 里写原因

────────────────────────────────────────────────────────────────
用法

    #include <Terminal/Process.h>
    using namespace X_Y;

    // 默认：输出直接进终端
    const auto result = Process::Run("git", {"status"});

    // 捕获：输出进 Result，不再打印
    Process::Options options;
    options.CaptureStdout = true;
    options.CaptureStderr = true;

    const auto captured = Process::Run("git", {"status"}, options);
    if (!captured.Started)
        Terminal::Write("[error] " + captured.Error + "\n");
    else
        Terminal::Write(captured.StdOut);
────────────────────────────────────────────────────────────────
*/

#include <string>
#include <string_view>
#include <vector>

namespace X_Y::Process
{
    struct Options
    {
        bool CaptureStdout = false;
        bool CaptureStderr = false;
    };

    struct Result
    {
        bool        Started  = false;
        int         ExitCode = -1;
        std::string Error;

        std::string StdOut;   // CaptureStdout 时有效
        std::string StdErr;   // CaptureStderr 时有效

        bool Ok() const
        {
            return Started && ExitCode == 0 && Error.empty();
        }
    };

    // arguments 不含 program 本身。
    // program 可以带路径，也可以只是名字（走 PATH 查找）。
    // 默认不捕获，子进程直接继承当前 stdin/stdout/stderr。
    Result Run(std::string_view program,
               const std::vector<std::string> &arguments,
               Options options = {});
}
