#include "APP/Application.h"
#include "APP/Library.h"
#include "APP/MicroProgram.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace
{
    enum class DemoEvent
    {
        Ping
    };

    class Ping final : public X_Y::App::XMovement
    {
    public:
        explicit Ping(X_Y::App::EventSender sender) : XMovement(sender) {}

        bool DispatchEvent(void *dispatcher) override
        {
            auto *d = static_cast<X_Y::App::EventDispatcher *>(dispatcher);
            return d->Dispatch(DemoEvent::Ping, this);
        }
    };

    // 自定义 Loop：只转 3 轮就返回 false，验证 Application 能被注入循环。
    class CountingLoop final : public X_Y::App::Loop
    {
    public:
        bool Pump() override
        {
            ++PumpCalls;
            return PumpCalls <= 3;
        }

        int PumpCalls = 0;
    };

    std::filesystem::path ExeDir(const char *argv0)
    {
        std::error_code ec;
        const auto self = std::filesystem::absolute(argv0, ec);
        if (ec)
        {
            return std::filesystem::current_path();
        }
        return self.parent_path();
    }
}

int main(int argc, char **argv)
{
    using namespace X_Y::App;

    int failures = 0;

    // ── Loop + EventBus + 延迟回收 ──────────────────────────────────────────
    auto loop = std::make_unique<CountingLoop>();
    auto *loopPtr = loop.get();

    Application app(std::move(loop));

    int hits = 0;
    app.GetDispatcher().Connect<DemoEvent>(
        nullptr, DemoEvent::Ping, nullptr,
        [&hits](const XMovement &)
        { ++hits; });

    bool recycled = false;
    app.DeferRecycle([&recycled]()
                     { recycled = true; });

    app.Push(new Ping(nullptr));
    app.Run();

    if (hits != 1)
    {
        std::fprintf(stderr, "[FAIL] EventBus: hits=%d，期望 1\n", hits);
        ++failures;
    }
    else
    {
        std::printf("[ OK ] EventBus: Ping 派发 1 次\n");
    }

    if (loopPtr->PumpCalls != 4)
    {
        std::fprintf(stderr, "[FAIL] Loop: PumpCalls=%d，期望 4（3 轮 + 退出探测）\n",
                     loopPtr->PumpCalls);
        ++failures;
    }
    else
    {
        std::printf("[ OK ] Loop: 注入循环推进 3 轮\n");
    }

    if (!recycled)
    {
        std::fprintf(stderr, "[FAIL] DeferRecycle: 安全点没有执行\n");
        ++failures;
    }
    else
    {
        std::printf("[ OK ] DeferRecycle: 安全点执行\n");
    }

    // ── Library：打开系统库并找一个符号 ────────────────────────────────────
    Library lib;
    std::string err;
#if defined(_WIN32)
    const bool opened = lib.Open("kernel32.dll", &err);
    auto getPid = opened ? reinterpret_cast<DWORD(WINAPI *)()>(
                               lib.Symbol("GetCurrentProcessId"))
                         : nullptr;
#else
    const bool opened = lib.Open("libc.so.6", &err);
    auto getPid = opened ? reinterpret_cast<pid_t (*)()>(
                               lib.Symbol("getpid"))
                         : nullptr;
#endif

    if (!opened || !getPid)
    {
        std::fprintf(stderr, "[FAIL] Library: %s\n", err.c_str());
        ++failures;
    }
    else
    {
        const auto pid = getPid();
        std::printf("[ OK ] Library: 打开 %s，pid=%llu\n",
                    lib.Path().c_str(),
                    static_cast<unsigned long long>(pid));
    }

    // ── MicroProgram：载入同目录的假微程序并调用 ────────────────────────────
#if defined(_WIN32)
    const std::filesystem::path plugin = ExeDir(argc > 0 ? argv[0] : "")
                                         / "AppDemoProgram.dll";
#else
    const std::filesystem::path plugin = ExeDir(argc > 0 ? argv[0] : "")
                                         / "AppDemoProgram.so";
#endif

    MicroProgram program;
    if (!program.Load(plugin.string(), MicroProgram::kDefaultEntry, &err))
    {
        std::fprintf(stderr, "[FAIL] MicroProgram: %s (%s)\n",
                     err.c_str(), plugin.string().c_str());
        ++failures;
    }
    else
    {
        const char *args[] = {"AppDemoProgram", nullptr};
        const int code = program.Run(nullptr, 1, args);
        if (code != 7)
        {
            std::fprintf(stderr, "[FAIL] MicroProgram: 退出码 %d，期望 7\n", code);
            ++failures;
        }
        else
        {
            std::printf("[ OK ] MicroProgram: 载入并调用 DLL 入口，退出码 7\n");
        }
    }

    std::printf("\n%s：%d 个失败\n", failures == 0 ? "[PASS]" : "[FAILED]",
                failures);
    return failures == 0 ? 0 : 1;
}
