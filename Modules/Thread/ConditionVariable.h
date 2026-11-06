#pragma once

#include "Thread/Mutex.h"

#include <chrono>
#include <condition_variable>
#include <utility>

namespace X_Y
{
    class ConditionVariable
    {
    public:
        ConditionVariable() = default;
        ConditionVariable(const ConditionVariable &) = delete;
        ConditionVariable &operator=(const ConditionVariable &) = delete;

        template <typename Predicate>
        void Wait(UniqueLock &lock, Predicate &&predicate)
        {
            m_Condition.wait(lock, std::forward<Predicate>(predicate));
        }

        template <typename Rep, typename Period, typename Predicate>
        bool WaitFor(
            UniqueLock &lock,
            const std::chrono::duration<Rep, Period> &duration,
            Predicate &&predicate)
        {
            return m_Condition.wait_for(
                lock, duration, std::forward<Predicate>(predicate));
        }

        void NotifyOne() noexcept { m_Condition.notify_one(); }
        void NotifyAll() noexcept { m_Condition.notify_all(); }

    private:
        std::condition_variable_any m_Condition;
    };
}
