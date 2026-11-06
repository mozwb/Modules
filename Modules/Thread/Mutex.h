#pragma once

#include <mutex>

namespace X_Y
{
    class Mutex
    {
    public:
        Mutex() = default;
        Mutex(const Mutex &) = delete;
        Mutex &operator=(const Mutex &) = delete;

        void lock() { m_Mutex.lock(); }
        bool try_lock() { return m_Mutex.try_lock(); }
        void unlock() { m_Mutex.unlock(); }

    private:
        std::mutex m_Mutex;
    };

    using LockGuard = std::lock_guard<Mutex>;
    using UniqueLock = std::unique_lock<Mutex>;
}
