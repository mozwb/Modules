# UI —— 原生 Panel / DockLayout / 组件 + ImGui 层。
#
# 依赖：Widget（窗口/Canvas/事件）、Media（播放器面板）、
#       DataStore/Decoder/Timer/Memory，以及 vendor 的 imgui 目标。
# UI 不直接依赖 APP；它通过 Widget/EventHelpers 和 Widget/WindowHost 使用 APP。

if(NOT TARGET Widget OR NOT TARGET Media)
    message(STATUS "UI: 缺少 Widget 或 Media，跳过")
    return()
endif()

if(NOT TARGET xy_vendor_imgui)
    message(STATUS "UI: 缺少 xy_vendor_imgui，跳过")
    return()
endif()

xy_module(UI STATIC DEPS
    Core
    Memory
    Timer
    DataStore
    Decoder
    Input
    Widget
    Media)

target_link_libraries(UI PUBLIC xy_vendor_imgui)

if(WIN32)
    target_link_libraries(UI PUBLIC opengl32)
endif()
