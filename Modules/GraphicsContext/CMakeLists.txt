# GraphicsContext —— 平台图形上下文抽象 + Win32 OpenGL 后端。
#
# 目前只有 Windows 的 wgl 实现；没有 glad 或非 Windows 时整模块跳过，
# 这样引擎其它模块仍能 configure。

if(NOT WIN32)
    message(STATUS "GraphicsContext: 目前只有 Win32 OpenGL 后端，跳过")
    return()
endif()

if(NOT TARGET xy_vendor_glad)
    message(STATUS "GraphicsContext: 没有 glad，跳过")
    return()
endif()

xy_module(GraphicsContext STATIC DEPS Log)

target_link_libraries(GraphicsContext PUBLIC
    xy_vendor_glad
    opengl32
    gdi32
    user32)
