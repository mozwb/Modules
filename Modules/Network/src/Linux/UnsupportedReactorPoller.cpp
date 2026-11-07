#include "Network/src/ReactorPoller.h"

#include <stdexcept>

namespace X_Y::detail
{
    std::unique_ptr<ReactorPoller> CreateReactorPoller()
    {
        throw std::runtime_error("IoContext Reactor backend is not implemented on this platform");
    }
}
