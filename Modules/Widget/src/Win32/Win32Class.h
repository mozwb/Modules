#pragma once
#include <windows.h>

namespace X_Y::Win32 {
	// 初始化并注册窗口类（可重复调用，内部防重入）
	void RegisterWinClass(HINSTANCE hInstance);

} // namespace X_Y::Win32