#include "Widget/Dpi.h"
#include <windows.h>

// GetDpiForSystem 需要 Win10 1607+，直接链接 user32 即可获取。

namespace X_Y {

    void Dpi::DeclareAware() {
        // 优先 PER_MONITOR_AWARE_V2（周边显示器各自精确），失败退化到
        // 系统级 aware，再失败用老的 SetProcessDPIAware。
        using PFN = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
        if (user32) {
            PFN pfn = (PFN)::GetProcAddress(user32,
                "SetProcessDpiAwarenessContext");
            if (pfn) {
                if (pfn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
                    return;
            }
        }
        // 退化路径
        using PFN2 = HRESULT(WINAPI*)(int);
        if (user32) {
            PFN2 pfn2 = (PFN2)::GetProcAddress(user32,
                "SetProcessDpiAwareness");
            if (pfn2) {
                if (pfn2(2) == S_OK)   // 2 = PROCESS_PER_MONITOR_DPI_AWARE
                    return;
            }
        }
        ::SetProcessDPIAware();
    }

    int Dpi::GetDpi() {
        // Win10 1607+ 有 GetDpiForSystem；老系统回退 96。
        using PFN = UINT(WINAPI*)();
        HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
        if (user32) {
            PFN pfn = (PFN)::GetProcAddress(user32, "GetDpiForSystem");
            if (pfn) {
                UINT dpi = pfn();
                if (dpi != 0)
                    return (int)dpi;
            }
        }
        HDC dc = ::GetDC(nullptr);
        int dpi = ::GetDeviceCaps(dc, LOGPIXELSX);
        ::ReleaseDC(nullptr, dc);
        return dpi > 0 ? dpi : 96;
    }

    float Dpi::GetScale() {
        return (float)GetDpi() / 96.0f;
    }

} // namespace X_Y
