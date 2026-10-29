#pragma once

/*
APP/MicroProgram.h
version 0.0.0

DLL 微程序的最小支持：载入 -> 找入口 -> 把 host / argv 交过去。

默认入口名是 X_Y 的 "xy_program_v1"，但这里不 include xy_api.h、
不认识 XyHostV1。入口签名只要求 C ABI 形状：

    int entry(const void* host, int argc, const char* const* argv);

以后 X_Y 的 ModuleRegistry 可以复用 Library 找 "xy_module_v1"；
本类只覆盖“跑一个程序”的场景。

不做：
  参数解析、host 表构造、ABI 版本检查 -> X_Y 侧
  线程/超时/挂起                          -> 直接阻塞调用，和 Terminal::Process 一样
*/

#include "APP/Library.h"

#include <string>
#include <vector>

namespace X_Y::App
{
    class MicroProgram
    {
    public:
        using Entry = int (*)(const void *host, int argc, const char *const *argv);

        inline static constexpr const char *kDefaultEntry = "xy_program_v1";

        MicroProgram() = default;
        ~MicroProgram() { Unload(); }

        MicroProgram(const MicroProgram &) = delete;
        MicroProgram &operator=(const MicroProgram &) = delete;

        // entry 为空时用 kDefaultEntry。失败时关闭库，不留下半载状态。
        bool Load(const std::string &path,
                  const char *entry = kDefaultEntry,
                  std::string *error = nullptr);
        void Unload();

        bool IsLoaded() const { return m_Entry != nullptr; }
        explicit operator bool() const { return IsLoaded(); }

        // 未载入时返回 -1。
        int Run(const void *host, int argc, const char *const *argv) const;

        // 便利重载：args[0] 就是 argv[0]，调用方自己决定。
        int Run(const void *host, const std::vector<std::string> &args) const;

        Library &GetLibrary() { return m_Library; }
        const Library &GetLibrary() const { return m_Library; }

    private:
        Library m_Library;
        Entry m_Entry = nullptr;
    };
}
