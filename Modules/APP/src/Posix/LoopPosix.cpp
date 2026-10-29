#include "APP/Loop.h"

#include <poll.h>

namespace X_Y::App
{
    namespace
    {
        // 暂时只做空转：以后 X_Y 的命令循环 / X11 后端可以替换这个 Loop。
        class PosixLoop final : public Loop
        {
        public:
            bool Pump() override
            {
                ::poll(nullptr, 0, 1);
                return true;
            }
        };
    }

    std::unique_ptr<Loop> CreatePlatformLoop()
    {
        return std::make_unique<PosixLoop>();
    }
}
