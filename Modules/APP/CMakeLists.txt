# APP —— 应用层内核：Loop + EventBus + Application + DLL 微程序加载。
#
# 不依赖 Widget / Movement / Terminal。平台差异只在 src/Win32 与 src/Posix，
# 由这里二选一，公开头里没有平台头。

if(WIN32)
    set(_exclude "src/Posix/")
else()
    set(_exclude "src/Win32/")
endif()

xy_module(APP STATIC DEPS Core EXCLUDE "${_exclude}")

# POSIX 的 dlopen/dlsym 在 libdl 里；Windows 上 CMAKE_DL_LIBS 为空。
target_link_libraries(APP PUBLIC ${CMAKE_DL_LIBS})
