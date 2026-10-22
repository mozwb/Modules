#include "Widget/PlatformLoop.h"
#include "Win32/Win32Class.h"
#include "winConfigure.h"
#include "Widget/Dpi.h"
#include <windows.h>

namespace X_Y {

class PlatformLoopWin32 : public PlatformLoop {
public:
    void Boot() override {
        // 平台启动引导：DPI aware + 窗口类注册 + 控制台编码/ANSI 颜色
        Dpi::DeclareAware();
        Win32::RegisterWinClass(::GetModuleHandle(nullptr));
        allowConsole();
    }

    bool PumpMessage() override {
        MSG msg{};
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            return msg.message != WM_QUIT;
        }
        return true;
    }
};

PlatformLoop* PlatformLoopFactory::Create() {
    return new PlatformLoopWin32();
}

} 
// namespace X_Y
