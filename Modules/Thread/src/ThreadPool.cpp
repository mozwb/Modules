#include "Thread/ThreadPool.h"

#include <algorithm>
#include <stdexcept>

namespace X_Y
{
    thread_local ThreadPool *ThreadPool::s_CurrentPool = nullptr;

    ThreadPool::ThreadPool(std::size_t workerCount)
    {
        if (workerCount == 0)
            workerCount = std::max(1u, std::thread::hardware_concurrency());

        m_Workers.reserve(workerCount);
        try
        {
            for (std::size_t i = 0; i < workerCount; ++i)
            {
                m_Workers.emplace_back();
                m_Workers.back().Start([this](std::stop_token)
                {
                    WorkerLoop();
                });
            }
        }
        catch (...)
        {
            Shutdown();
            throw;
        }
    }

    ThreadPool::~ThreadPool()
    {
        Shutdown();
    }

    void ThreadPool::Shutdown()
    {
        if (s_CurrentPool == this)
            throw std::logic_error("ThreadPool::Shutdown cannot be called by its own task");

        std::lock_guard<std::mutex> shutdownLock(m_ShutdownMutex);
        {
            LockGuard lock(m_Mutex);
            m_Stopping = true;
        }
        m_Condition.NotifyAll();

        for (Thread &worker : m_Workers)
            worker.Join();
    }

    bool ThreadPool::IsShutdown() const
    {
        LockGuard lock(m_Mutex);
        return m_Stopping;
    }

    void ThreadPool::WorkerLoop()
    {
        s_CurrentPool = this;
        for (;;)
        {
            std::function<void()> task;
            {
                UniqueLock lock(m_Mutex);
                m_Condition.Wait(lock, [this] { return m_Stopping || !m_Tasks.empty(); });
                if (m_Tasks.empty())
                {
                    s_CurrentPool = nullptr;
                    return;
                }

                task = std::move(m_Tasks.front());
                m_Tasks.pop();
            }

            task();
        }
    }
}
