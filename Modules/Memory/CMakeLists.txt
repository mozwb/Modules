# Memory —— 通用分配器 + Buffer。
#
# 作为第三方共享库构建：X_Y、IO 和 consumer 共用同一个 Memory.dll。
# 不再重载全局 operator new/delete（XMemGlobalNew.cpp 不编进 DLL）；
# 开发者显式使用 X_Y::New / Delete / Malloc / Free / Buffer。
#
# 这里临时关掉严格导出：Memory.dll 要导出 C++ 符号（Memory::Instance、
# allocate/deallocate、Buffer 实现等），不能只导出 XY_EXPORT。
set(_xy_memory_strict "${XY_STRICT_EXPORTS}")
set(XY_STRICT_EXPORTS OFF)
xy_module(Memory SHARED EXCLUDE "src/XMemGlobalNew\\.cpp")
set(XY_STRICT_EXPORTS "${_xy_memory_strict}")

# src/XMemProcess.cpp 查进程内存：Windows 走 GetProcessMemoryInfo（psapi）。
# 只在 Windows 上需要；Linux 那边读 /proc，不欠系统库。
if(WIN32)
    target_link_libraries(Memory PUBLIC psapi)
endif()
