#pragma once
#include <windows.h>
#include <tchar.h>

namespace X_Y::Win32 {

    // 程序实例句柄（由 WinMain 设置）

    inline HINSTANCE g_hInstance = nullptr;

    // 窗口类名
    inline const TCHAR* g_szClassName = _T("X_YWindow");

    // WndProc hook（ImGui 消息拦截）
    using WndProcHookFn = LRESULT(*)(HWND, UINT, WPARAM, LPARAM);
    inline WndProcHookFn g_WndProcHook = nullptr;

} 
// namespace X_Y::Win32
