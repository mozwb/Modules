#pragma once

#include "Network/IoContext.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace X_Y::detail
{
    struct PollerEvent
    {
        std::uint64_t id = 0;
        IoReady ready = IoReady::Error;
    };

    class ReactorPoller
    {
    public:
        virtual ~ReactorPoller() = default;

        virtual void Add(
            std::uint64_t id,
            std::uintptr_t nativeSocket,
            IoInterest interest) = 0;
        virtual bool Update(std::uint64_t id, IoInterest interest) = 0;
        virtual void Remove(std::uint64_t id) noexcept = 0;
        virtual std::vector<PollerEvent> Wait(std::chrono::milliseconds timeout) = 0;
        virtual void Wake() = 0;
    };

    std::unique_ptr<ReactorPoller> CreateReactorPoller();
}
