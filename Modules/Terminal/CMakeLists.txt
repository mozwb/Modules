# Terminal —— 终端能力 + 指令层：探测 / 样式 / 输出 / 解析与分派（屏幕与读键后面加）。
#
# 平台：src/Win32 与 src/Posix 二选一，其余源码共用。
# 控制台模式是进程级状态，固定 STATIC，不跟 -DXY_MODULE_LINKAGE 走。

if(WIN32)
    set(_exclude "src/Posix/")
else()
    set(_exclude "src/Win32/")
endif()

xy_module(Terminal STATIC DEPS Core EXCLUDE "${_exclude}")
