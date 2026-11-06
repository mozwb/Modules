#pragma once

#include "Thread/ConditionVariable.h"
#include "Thread/Thread.h"

#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace X_Y
{
    class ThreadPool
    {
    public:
        // A zero worker count selects hardware_concurrency(), with a minimum of one.
        explicit ThreadPool(std::size_t workerCount = 0);
        ~ThreadPool();

        ThreadPool(const ThreadPool &) = delete;
        ThreadPool &operator=(const ThreadPool &) = delete;
        ThreadPool(ThreadPool &&) = delete;
        ThreadPool &operator=(ThreadPool &&) = delete;

        template <typename Function, typename... Args>
        auto Submit(Function &&function, Args &&...args)
            -> std::future<std::invoke_result_t<
                std::decay_t<Function>,
                std::decay_t<Args>...>>
        {
            // std::invoke_result_t推导出来返回的类型是啥
            // std::decay_t<T>是去掉引用和cv修饰符后的类型
            using Result = std::invoke_result_t<
                std::decay_t<Function>,
                std::decay_t<Args>...>;

            // lambda 基本语法：[捕获列表] (形参列表) mutable -> 返回类型 { 函数体 }
            auto boundTask =
                [callable = std::decay_t<Function>(std::forward<Function>(function)),
                 arguments = std::tuple<std::decay_t<Args>...>(
                     std::forward<Args>(args)...)]() mutable -> Result
            {
                // auto t = std::tuple{1,2};
                // std::apply(add, t); // 等价 add(1,2)
                return std::apply(
                    [&callable](auto &&...unpacked) -> Result
                    {
                        return std::invoke(
                            std::move(callable),
                            std::forward<decltype(unpacked)>(unpacked)...);
                    },
                    std::move(arguments));
            };

            auto task = std::make_shared<std::packaged_task<Result()>>(
                std::move(boundTask));
            std::future<Result> result = task->get_future();

            {
                LockGuard lock(m_Mutex);
                if (m_Stopping)
                    throw std::runtime_error("Cannot submit to a stopped ThreadPool");

                m_Tasks.emplace([task]
                                { (*task)(); });
            }

            m_Condition.NotifyOne();
            return result;
        }

        // Stops accepting tasks, drains queued work, and joins all workers.
        // Must not be called from a task running in this pool.
        void Shutdown();

        std::size_t WorkerCount() const noexcept { return m_Workers.size(); }
        bool IsShutdown() const;
        bool IsWorkerThread() const noexcept { return s_CurrentPool == this; }

    private:
        void WorkerLoop();

        mutable Mutex m_Mutex;
        ConditionVariable m_Condition;
        std::queue<std::function<void()>> m_Tasks;
        std::vector<Thread> m_Workers;
        bool m_Stopping = false;
        std::mutex m_ShutdownMutex;

        static thread_local ThreadPool *s_CurrentPool;
    };
}
