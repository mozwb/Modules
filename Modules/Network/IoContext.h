#pragma once

#include "Network/SocketHandle.h"

#include <cstdint>
#include <functional>
#include <memory>

namespace X_Y
{
    enum class IoInterest : std::uint8_t
    {
        Read = 1,
        Write = 2,
        ReadWrite = 3
    };

    enum class IoReady : std::uint8_t
    {
        Read = 1,
        Write = 2,
        Error = 4,
        Closed = 8
    };

    constexpr IoReady operator|(IoReady left, IoReady right) noexcept
    {
        return static_cast<IoReady>(
            static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
    }

    constexpr bool HasIoReady(IoReady value, IoReady flag) noexcept
    {
        return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(flag)) != 0;
    }

    namespace detail
    {
        class IoContextState;
    }

    class IoContext
    {
    public:
        using Handler = std::function<void(IoReady)>;

        class Registration
        {
        public:
            Registration() = default;
            ~Registration();

            Registration(const Registration &) = delete;
            Registration &operator=(const Registration &) = delete;
            Registration(Registration &&other) noexcept;
            Registration &operator=(Registration &&other) noexcept;

            bool Update(IoInterest interest);
            void Reset() noexcept;
            explicit operator bool() const noexcept { return m_Id != 0; }

        private:
            friend class IoContext;

            Registration(
                std::weak_ptr<detail::IoContextState> state,
                std::uint64_t id) noexcept;

            std::weak_ptr<detail::IoContextState> m_State;
            std::uint64_t m_Id = 0;
        };

        IoContext();
        ~IoContext();

        IoContext(const IoContext &) = delete;
        IoContext &operator=(const IoContext &) = delete;
        IoContext(IoContext &&) = delete;
        IoContext &operator=(IoContext &&) = delete;

        Registration Watch(
            SocketHandle &socket,
            IoInterest interest,
            Handler handler);

        void Run();
        void Stop() noexcept;
        bool IsStopped() const noexcept;

    private:
        std::shared_ptr<detail::IoContextState> m_State;
    };
}
