# imgui —— 核心 + Win32/OpenGL3 backends，编成静态库。
#
# 放进 vendor 后，UI/Render 只依赖目标 xy_vendor_imgui，不再自己
# glob imgui 源文件。非 Windows 暂时跳过。

if(NOT WIN32)
    return()
endif()

set(_imgui_root "${CMAKE_CURRENT_LIST_DIR}/../imgui")
if(NOT EXISTS "${_imgui_root}/imgui.cpp")
    return()
endif()

if(NOT TARGET xy_vendor_imgui)
    add_library(xy_vendor_imgui STATIC
        "${_imgui_root}/imgui.cpp"
        "${_imgui_root}/imgui_draw.cpp"
        "${_imgui_root}/imgui_tables.cpp"
        "${_imgui_root}/imgui_widgets.cpp"
        "${_imgui_root}/imgui_demo.cpp"
        "${_imgui_root}/backends/imgui_impl_win32.cpp"
        "${_imgui_root}/backends/imgui_impl_opengl3.cpp")

    target_include_directories(xy_vendor_imgui PUBLIC
        "${_imgui_root}/../.."
        "${_imgui_root}"
        "${_imgui_root}/backends")

    target_link_libraries(xy_vendor_imgui PUBLIC
        opengl32
        gdi32
        user32)

    if(DEFINED XY_LIB_OUT_DIR)
        set_target_properties(xy_vendor_imgui PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY "${XY_LIB_OUT_DIR}")
    endif()
endif()

unset(_imgui_root)
