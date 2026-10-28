#pragma once

/*
Command.h
version 0.0.0

Terminal 模块的上一层：一条指令的解析与分派。
Terminal 只发字节，Command 才认识「命令」。

────────────────────────────────────────────────────────────────
做

  Parse      一行字 -> Args{Name, Items}
             支持空白分隔、单/双引号、反斜杠转义、相邻段拼接。
  Registry   名字 -> handler 的表，Run 负责解析、查表、调用。
  Result     ExitCode / Started / Error，不抛异常。

不做

  外部进程本身（dir / git / ...） -> Process；Command 只在查不到内建命令时转过去
  提示符、历史、行编辑、循环   -> Shell
  内建命令的具体内容           -> 使用端自己注册
  输出                          -> handler 自己调 Terminal::Write

────────────────────────────────────────────────────────────────
约定

  . 查表时命令名转小写，Args.Name 保留原样；Windows / Linux 行为一致。
  . 同名注册直接覆盖；空名字、空 handler 注册失败。
  . 查不到内建命令时默认交给 Process 跑外部程序；SetExternalEnabled(false) 关掉。
  . 退出码：0 成功，2 解析失败，127 找不到命令 / 启动失败，其他由 handler 或外部程序决定。
  . v1 不加锁：注册和执行归同一线程。

────────────────────────────────────────────────────────────────
用法

    #include <Terminal/Command.h>
    #include <Terminal/Terminal.h>

    X_Y::Command::Registry registry;

    registry.Add("echo", [](const X_Y::Command::Args &args)
    {
        std::string line;
        for (const auto &item : args.Items)
        {
            if (!line.empty())
                line += ' ';
            line += item;
        }
        X_Y::Terminal::Write(line + "\n");
        return X_Y::Command::Result{};
    });

    const auto result = registry.Run("echo \"hello world\"");
    if (!result.Error.empty())
        X_Y::Terminal::Write("[error] " + result.Error + "\n");
────────────────────────────────────────────────────────────────
*/

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace X_Y::Command
{
    struct Args
    {
        std::string              Name;    // 命令名，保留输入大小写
        std::vector<std::string> Items;   // 参数，不含命令名
    };

    struct Result
    {
        int         ExitCode = 0;
        bool        Started  = true;   // 找到并执行了；解析失败 / 未知命令为 false
        std::string Error;

        bool Ok() const
        {
            return Started && Error.empty() && ExitCode == 0;
        }
    };

    // 解析一行。失败时 out 清空；error 可为 nullptr。
    // 规则：
    //   . 空白分隔；单引号里的内容原样保留；双引号里支持转义的引号和反斜杠
    //   . 引号外 \x 转义成 x；相邻的引号段和普通段拼成同一个参数
    //   . 空行、没闭合的引号、结尾单独一个 \ 都算解析失败
    bool Parse(std::string_view line, Args &out, std::string *error = nullptr);

    class Registry
    {
    public:
        using Handler = std::function<Result(const Args &)>;

        // 覆盖同名命令。name 为空或 handler 为空时返回 false。
        bool Add(std::string name, Handler handler);
        bool Remove(std::string_view name);
        bool Has(std::string_view name) const;

        // 已按名字排序（都是小写）。
        std::vector<std::string> Names() const;

        // 查不到内建命令时是否交给 Process 跑外部程序。默认开。
        void SetExternalEnabled(bool on);
        bool ExternalEnabled() const;

        Result Run(std::string_view line);
        Result Run(const Args &args);

    private:
        std::map<std::string, Handler> _handlers;
        bool                           _external = true;
    };
}
