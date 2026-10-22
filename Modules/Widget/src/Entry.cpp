#include "Log/XYLog.h"
#ifdef XY_PLATFORM_WINDOWS
#include "Win32/Win32Class.h"
#include "winConfigure.h"
#include "Widget/Dpi.h"
extern int main(int argc, char *argv[]);
int WINAPI WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nCmdShow)
{
    X_Y::Dpi::DeclareAware(); // 声明 DPI-aware，消除系统位图拉伸导致的模糊
    X_Y::allowConsole();
    XINFO("程序运行到入口")
    X_Y::Win32::RegisterWinClass(hInstance);
    return main(__argc, __argv);
}
#else
#error "仅支持windows系统"
#endif
