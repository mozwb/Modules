#include "APP/Loop.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace X_Y::App
{
    namespace
    {
        class Win32Loop final : public Loop
        {
        public:
            bool Pump() override
            {
                MSG msg{};
                if (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
                {
                    ::TranslateMessage(&msg);
                    ::DispatchMessage(&msg);
                    return msg.message != WM_QUIT;
                }
                return true;
            }
        };
    }

    std::unique_ptr<Loop> CreatePlatformLoop()
    {
        return std::make_unique<Win32Loop>();
    }
}
