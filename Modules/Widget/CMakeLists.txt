# Widget —— 窗口系统 + 具体事件类型 + 窗口生命周期宿主。
#
# 依赖：Core / Log / Files / Input / GraphicsContext / DataStore（Log debug 后端）
#       freetype（文字后端）
# 不依赖 UI；UI 反过来依赖 Widget。

if(NOT WIN32)
    message(STATUS "Widget: 目前只有 Win32 窗口实现，跳过")
    return()
endif()

foreach(_dep Core Log Files Input GraphicsContext DataStore)
    if(NOT TARGET ${_dep})
        message(STATUS "Widget: 缺少依赖 ${_dep}，跳过")
        return()
    endif()
endforeach()

if(NOT TARGET freetype)
    message(STATUS "Widget: 缺少 freetype，跳过")
    return()
endif()

xy_module(Widget STATIC DEPS Core Log Files Input GraphicsContext DataStore)

# BaseWin.h / Font.h 直接 include 同目录下的 WindowImpl.h / CanvasImpl.h。
target_include_directories(Widget PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(Widget PUBLIC
    freetype
    opengl32
    gdi32
    user32
    ole32
    shell32
    comdlg32)
