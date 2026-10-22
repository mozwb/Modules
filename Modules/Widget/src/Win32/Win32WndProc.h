#pragma once
#include <windows.h>

namespace X_Y::Win32 {

	// 主窗口过程函数
	LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

} // namespace X_Y::Win32