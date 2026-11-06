#pragma once

#include <concepts>
#include <functional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace X_Y
{
    class Thread
    {
    public:
        Thread() = default;
        ~Thread() = default;

        Thread(const Thread &) = delete;
        Thread &operator=(const Thread &) = delete;
        Thread(Thread &&) noexcept = default;
        Thread &operator=(Thread &&) noexcept = default;

        // The task may accept std::stop_token to cooperate with shutdown.
        template <typename Function>
            requires std::invocable<std::decay_t<Function> &> ||
                     std::invocable<std::decay_t<Function> &, std::stop_token>
        void Start(Function &&function)
        {
            if (m_Thread.joinable())
                throw std::logic_error("Thread is already joinable");

            m_Thread = std::jthread(std::forward<Function>(function));
        }

        bool RequestStop() noexcept
        {
            return m_Thread.request_stop();
        }

        void Join();
        bool IsJoinable() const noexcept { return m_Thread.joinable(); }
        std::thread::id GetId() const noexcept { return m_Thread.get_id(); }

    private:
        std::jthread m_Thread;
    };
}
