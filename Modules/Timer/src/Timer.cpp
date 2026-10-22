#include "Timer/Timer.h"

// <chrono> / <thread> / <mutex> / <condition_variable> 全部锁在这里，
// 不进入 Timer.h —— 它们合起来会让每个 include 者多付 600ms 以上。
#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <mutex>
#include <thread>

namespace X_Y
{

    namespace
    {
        using SteadyClock = std::chrono::steady_clock;

        inline long long SteadyNowNs()
        {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(
                       SteadyClock::now().time_since_epoch())
                .count();
        }
    } // namespace

    // ════════════════════════════════════════════════════════════════════════
    // StopWatch
    // ════════════════════════════════════════════════════════════════════════

    StopWatch::StopWatch()
    {
        Reset();
    }
    StopWatch::~StopWatch()
    {
        // 析构留空 —— 日志由 ProfileScope 等 RAII 包装类统一处理
    }
    void StopWatch::Reset()
    {
        m_StartNs = SteadyNowNs();
        m_AccumulateNs = 0;
        m_IsPaused = false;
    }

    void StopWatch::Pause()
    {
        if (m_IsPaused)
            return;

        // 累加本次运行时长
        m_AccumulateNs += SteadyNowNs() - m_StartNs;
        m_IsPaused = true;
    }

    void StopWatch::Resume()
    {
        if (!m_IsPaused)
            return;

        m_StartNs = SteadyNowNs();
        m_IsPaused = false;
    }

    long long StopWatch::Nanoseconds()
    {
        long long total = m_AccumulateNs;
        if (!m_IsPaused)
            total += SteadyNowNs() - m_StartNs;
        return total;
    }

    float StopWatch::Microseconds()
    {
        return static_cast<float>(Nanoseconds()) / 1000.0f;
    }

    float StopWatch::Milliseconds()
    {
        return static_cast<float>(Nanoseconds()) / 1000000.0f;
    }

    float StopWatch::Seconds()
    {
        return static_cast<float>(Nanoseconds()) / 1000000000.0f;
    }

    float StopWatch::Measure(std::function<void()> func)
    {
        StopWatch watch;
        func();
        return watch.Milliseconds();
    }

    // ════════════════════════════════════════════════════════════════════════
    // SysClock
    // ════════════════════════════════════════════════════════════════════════

    TimeData SysClock::Now()
    {
        auto tp = std::chrono::system_clock::now();
        std::time_t stamp = std::chrono::system_clock::to_time_t(tp);
        std::tm local = *std::localtime(&stamp);

        TimeData data{};
        data.year = local.tm_year + 1900;
        data.month = local.tm_mon + 1;
        data.day = local.tm_mday;
        data.hour = local.tm_hour;
        data.minute = local.tm_min;
        data.second = local.tm_sec;
        return data;
    }

    std::string SysClock::ToTwoDigit(int num)
    {
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(2) << num;
        return oss.str();
    }

    std::string SysClock::Format(const TimeData &data, const std::string &pattern)
    {
        std::string output = pattern;
        std::string y4 = std::to_string(data.year);
        std::string m2 = ToTwoDigit(data.month);
        std::string d2 = ToTwoDigit(data.day);
        std::string h2 = ToTwoDigit(data.hour);
        std::string min2 = ToTwoDigit(data.minute);
        std::string s2 = ToTwoDigit(data.second);

        // 依次替换占位符
        size_t pos;
        // YY 完整4位年
        while ((pos = output.find("YY")) != std::string::npos)
            output.replace(pos, 2, y4);
        // MM 月份
        while ((pos = output.find("MM")) != std::string::npos)
            output.replace(pos, 2, m2);
        // DD 日期
        while ((pos = output.find("DD")) != std::string::npos)
            output.replace(pos, 2, d2);
        // hh 小时
        while ((pos = output.find("hh")) != std::string::npos)
            output.replace(pos, 2, h2);
        // mm 分钟
        while ((pos = output.find("mm")) != std::string::npos)
            output.replace(pos, 2, min2);
        // ss 秒
        while ((pos = output.find("ss")) != std::string::npos)
            output.replace(pos, 2, s2);

        return output;
    }

    std::string SysClock::NowFormat(const std::string &pattern)
    {
        return Format(Now(), pattern);
    }

    std::string SysClock::ToString(const TimeData &data)
    {
        return Format(data, "YY-MM-DD hh:mm:ss");
    }

    std::string SysClock::NowString()
    {
        return ToString(Now());
    }

    // ════════════════════════════════════════════════════════════════════════
    // Ticker
    // ════════════════════════════════════════════════════════════════════════

    struct Ticker::Impl
    {
        std::thread thread;
        std::atomic<bool> running{false};
        std::atomic<bool> paused{false};
        std::mutex mutex;
        std::condition_variable cv;
        int intervalMs = 0;
        std::function<void()> callback;
    };

    Ticker::Ticker() : m_Impl(std::make_unique<Impl>()) {}

    Ticker::~Ticker()
    {
        Stop();
        Join();
    }

    void Ticker::Start(int intervalMs, std::function<void()> callback)
    {
        Stop();
        Join();

        m_Impl->intervalMs = intervalMs;
        m_Impl->callback = std::move(callback);
        m_Impl->running = true;
        m_Impl->paused = false;

        // 线程只认启动那一刻的副本，避免运行中改参数造成竞争
        const int interval = m_Impl->intervalMs;
        std::function<void()> callbackCopy = m_Impl->callback;

        m_Impl->thread = std::thread(
            [this, interval, callbackCopy]()
            {
                while (m_Impl->running)
                {
                    std::unique_lock<std::mutex> lock(m_Impl->mutex);
                    m_Impl->cv.wait_for(lock, std::chrono::milliseconds(interval),
                                        [this]()
                                        { return !m_Impl->running.load(); });
                    lock.unlock();

                    if (!m_Impl->running)
                        break;
                    if (m_Impl->paused)
                        continue;

                    callbackCopy();
                }
            });
    }

    void Ticker::Stop()
    {
        m_Impl->running = false;
        m_Impl->cv.notify_one();
    }

    void Ticker::Join()
    {
        if (m_Impl->thread.joinable())
        {
            m_Impl->thread.join();
        }
    }

    void Ticker::Pause()
    {
        m_Impl->paused = true;
    }

    void Ticker::Resume()
    {
        m_Impl->paused = false;
    }

    void Ticker::Restart()
    {
        if (m_Impl->callback)
        {
            Stop();
            Join();
            Start(m_Impl->intervalMs, m_Impl->callback);
        }
    }

    void Ticker::Restart(int intervalMs, std::function<void()> callback)
    {
        Stop();
        Join();
        Start(intervalMs, std::move(callback));
    }

    void Ticker::Detach()
    {
        m_Impl->running = false;
        if (m_Impl->thread.joinable())
        {
            m_Impl->thread.detach();
        }
    }

    bool Ticker::IsRunning() const { return m_Impl->running.load(); }
    bool Ticker::IsPaused() const { return m_Impl->paused.load(); }
    bool Ticker::IsJoinable() const { return m_Impl->thread.joinable(); }

    // ════════════════════════════════════════════════════════════════════════
    // Sleep
    // ════════════════════════════════════════════════════════════════════════

    void Sleep(int milliseconds)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }

    void Sleep(float seconds)
    {
        std::this_thread::sleep_for(std::chrono::duration<float>(seconds));
    }

    // ════════════════════════════════════════════════════════════════════════
    // FrameClock
    // ════════════════════════════════════════════════════════════════════════

    FrameClock::FrameClock()
    {
        m_LastTickNs = SteadyNowNs();
    }

    float FrameClock::Tick()
    {
        const long long now = SteadyNowNs();

        if (m_First)
        {
            m_First = false;
            m_LastTickNs = now;
            return 0.0f; // 首帧不产生 dt
        }

        // 1) 算本帧耗时
        float dt = static_cast<float>(now - m_LastTickNs) / 1000000000.0f;
        m_LastTickNs = now;

        // 2) 上限钳制
        if (m_MaxDelta > 0.0f && dt > m_MaxDelta)
            dt = m_MaxDelta;

        // 3) 目标帧率：等到该帧应输出的时刻再返回
        if (m_TargetFps > 0.0f)
        {
            const float frameMs = 1000.0f / m_TargetFps; // 每帧应占毫秒
            const float usedMs = dt * 1000.0f;           // 本帧已用毫秒
            if (usedMs < frameMs)
                Sleep(frameMs - usedMs);
        }

        return dt;
    }

    void FrameClock::SetMaxDelta(float seconds) { m_MaxDelta = seconds; }
    float FrameClock::GetMaxDelta() const { return m_MaxDelta; }
    void FrameClock::SetTargetFPS(float fps) { m_TargetFps = fps; }
    float FrameClock::GetTargetFPS() const { return m_TargetFps; }

} // namespace X_Y
