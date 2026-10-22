#pragma once

/*
Timer.h
version 1.0.2

计时相关：秒表、系统时间、帧时钟、周期定时器、线程睡眠。

零依赖模块（除标准库外不依赖任何 X_Y 模块）。

────────────────────────────────────────────────────────────────
关于头文件重量

本头刻意不 include <chrono> / <thread> / <mutex> / <condition_variable>：
实测这四者在本机单独解析合计 600ms 以上，而本头会被很多 .cpp include。

  . StopWatch / FrameClock 内部只存 long long 纳秒，chrono 只出现在 Timer.cpp
  . Ticker 的线程/互斥量/条件变量藏在 Timer.cpp 里的 Impl（pimpl）

所以别把 chrono 类型加回成员变量里 —— 那样每个 include 本头的人都会变慢。
────────────────────────────────────────────────────────────────
用法

    #include <Timer/Timer.h>
    using namespace X_Y;

    // 秒表
    StopWatch sw;
    sw.Milliseconds();

    // 主循环
    FrameClock clock;
    clock.SetTargetFPS(144.0f);
    while (running)
    {
        float dt = clock.Tick();   // 已钳制最大值、已按目标帧率 sleep
    }

    // 周期任务（独立线程）
    Ticker ticker;
    ticker.Start(100, [] { ... });   // 每 100ms 回调一次
    ticker.Stop();
    ticker.Join();
────────────────────────────────────────────────────────────────
*/

#include <functional>
#include <memory>
#include <string>

namespace X_Y
{

    // ── 秒表 ────────────────────────────────────────────────────────────────
    class StopWatch
    {
    public:
        StopWatch();
        ~StopWatch();

        void Reset();

        void Pause();
        void Resume();

        float Seconds();
        float Milliseconds();
        float Microseconds();
        long long Nanoseconds();

        // 跑一次 func 并返回耗时（毫秒）
        static float Measure(std::function<void()> func);

    private:
        long long m_StartNs = 0;
        long long m_AccumulateNs = 0;
        bool m_IsPaused = false;
    };

    // ── 拆好的系统时间 ──────────────────────────────────────────────────────
    struct TimeData
    {
        int year;   // 完整4位年份 2026
        int month;  // 1~12
        int day;    // 1~31
        int hour;   // 0~23
        int minute; // 0~59
        int second; // 0~59
    };

    // ── 系统时钟 ────────────────────────────────────────────────────────────
    class SysClock
    {
    public:
        // 获取当前本地系统时间
        static TimeData Now();

        /**
         * @brief 自定义格式化时间字符串
         * 占位符规则：
         * YY = 4位完整年份
         * MM = 两位月份(01~12)
         * DD = 两位日期(01~31)
         * hh = 两位小时(00~23)
         * mm = 两位分钟(00~59)
         * ss = 两位秒数(00~59)
         * 示例模板："YY-MM-DD hh:mm:ss" → "2026-06-18 14:25:09"
         */
        static std::string Format(const TimeData &data, const std::string &pattern);
        // 直接格式化当前时间
        static std::string NowFormat(const std::string &pattern);

        // 默认格式 YY-MM-DD hh:mm:ss
        static std::string ToString(const TimeData &data);
        static std::string NowString();

    private:
        // 数字转两位字符串，自动前补0
        static std::string ToTwoDigit(int num);
    };

    // ── 一帧的时长（秒），可隐式转 float ─────────────────────────────────────
    class Timestep
    {
    public:
        Timestep(float time = 0.0f)
            : m_Time(time)
        {
        }

        operator float() const { return m_Time; }

        float GetSeconds() const { return m_Time; }
        float GetMilliseconds() const { return m_Time * 1000.0f; }

    private:
        float m_Time;
    };

    // ════════════════════════════════════════════════════════════════════════
    // Ticker — 周期定时器，独立线程回调
    // ════════════════════════════════════════════════════════════════════════
    class Ticker
    {
    public:
        Ticker();
        ~Ticker();

        // 禁止拷贝
        Ticker(const Ticker &) = delete;
        Ticker &operator=(const Ticker &) = delete;

        // ── 启动与结束 ──
        void Start(int intervalMs, std::function<void()> callback);
        void Stop(); // 请求停止，不等
        void Join(); // 等线程完全结束

        // ── 临时控制 ──
        void Pause();                                                 // 暂停 callback，线程还在
        void Resume();                                                // 恢复 callback
        void Restart();                                               // 重启（已有参数）
        void Restart(int intervalMs, std::function<void()> callback); // 重启并换参数

        // ── 放生 ──
        void Detach();

        // ── 状态 ──
        bool IsRunning() const;
        bool IsPaused() const;
        bool IsJoinable() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    // ── 线程睡眠 ────────────────────────────────────────────────────────────
    void Sleep(int milliseconds);
    void Sleep(float seconds);

    // ── 帧时钟 ──────────────────────────────────────────────────────────────
    class FrameClock
    {
    public:
        FrameClock();

        // 记录一帧结束：返回本帧耗时(秒)。
        // dt 会被限制在 [0, 最大dt上限] 内，防卡顿跳帧导致物理爆炸。
        float Tick();

        // ── 配置（运行时设置，立即生效）──
        // 设置最大 dt(秒)。超过则钳制。0 或负 = 不限。
        void SetMaxDelta(float maxDelta);
        float GetMaxDelta() const;

        // 设置目标帧率(帧/秒)。Tick() 内部会 sleep 到该节奏。
        // 0 或负 = 不限制帧率。
        void SetTargetFPS(float fps);
        float GetTargetFPS() const;

    private:
        long long m_LastTickNs = 0;
        float m_MaxDelta = 0.05f; // 默认最大 dt 0.05s(20fps下限)
        float m_TargetFps = 0.0f; // 默认不限制
        bool m_First = true;      // 第一次 Tick 只初始化，不产生 dt
    };

} // namespace X_Y
