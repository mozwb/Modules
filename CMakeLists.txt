# ==============================================================================
# X_Y — 标准 CMake 构建（重构版）
# 目录约定：
#   Modules/<Name>/include   对外头文件
#   Modules/<Name>/src       实现
#   vendor/                  第三方(glad/glm/imgui)
#   dist/lib/                编译产物：mingw/ 与 msvc/ 分开放
#   dist/include/            对外头文件汇集(构建后)
# ==============================================================================

cmake_minimum_required(VERSION 3.20)
project(X_Y LANGUAGES C CXX)

include(ExternalProject)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# ── 全局编译定义 ─────────────────────────────────
if(WIN32)
    add_compile_definitions(XY_PLATFORM_WINDOWS UNICODE _UNICODE)
elseif(UNIX AND NOT APPLE)
    add_compile_definitions(XY_PLATFORM_LINUX)
elseif(APPLE)
    add_compile_definitions(XY_PLATFORM_MAC)
endif()

# 构建配置宏（Debug/Release/Dist）
add_compile_definitions($<$<CONFIG:Debug>:XY_DEBUG>)
add_compile_definitions($<$<CONFIG:Release>:XY_RELEASE>)
add_compile_definitions($<$<CONFIG:Dist>:XY_DIST>)

# ── UTF-8 编译（源码含中文注释，必须全链路 UTF-8）──
if(MSVC)
    add_compile_options(/utf-8)
else()
    add_compile_options(-finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

# ── 输出目录：dist/lib/<toolset> ─────────────────
if(MSVC)
    set(XY_LIB_DIR "${PROJECT_SOURCE_DIR}/dist/lib/msvc")
else()
    set(XY_LIB_DIR "${PROJECT_SOURCE_DIR}/dist/lib/mingw")
endif()

# ══════════════════════════════════════════════════════
# 辅助函数：定义一个 X_Y 模块静态库
#   xy_module(NAME)   → 用 Modules/NAME 下的 include + src
# ══════════════════════════════════════════════════════
function(xy_module NAME)
    set(_src_dir "${PROJECT_SOURCE_DIR}/Modules/${NAME}")
    file(GLOB_RECURSE _src CONFIGURE_DEPENDS
        "${_src_dir}/src/*.cpp"
        "${_src_dir}/src/*.h"
        "${_src_dir}/src/*.c")

    add_library(${NAME} STATIC ${_src})

    target_include_directories(${NAME} PUBLIC
        "${_src_dir}/include"                 # 对外头文件：#include <Network/...>
        "${_src_dir}"                        # 本模块根：内部 #include "Canvas.h"
        "${PROJECT_SOURCE_DIR}/Modules"       # 跨模块：#include "Widget/Canvas.h"
        "${PROJECT_SOURCE_DIR}")              # vendor/... 从 X_Y 根解析

    set_target_properties(${NAME} PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${XY_LIB_DIR}")
endfunction()

# ══════════════════════════════════════════════════════
# vendor：glad(静态库) / glm(header-only，提供 include) / imgui(并入需要它的模块)
# ══════════════════════════════════════════════════════
add_library(glad STATIC "${PROJECT_SOURCE_DIR}/vendor/glad/src/glad.c")
target_include_directories(glad PUBLIC
    "${PROJECT_SOURCE_DIR}/vendor/glad/include")
set_target_properties(glad PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${XY_LIB_DIR}")

# ══════════════════════════════════════════════════════
# 模块（按迁移进度逐步加入）
# ── Core 底层 ──
set(_xy_ffmpeg_source_dir "${PROJECT_SOURCE_DIR}/vendor/FFmpeg")
if(NOT EXISTS "${_xy_ffmpeg_source_dir}/configure")
    message(FATAL_ERROR
        "FFmpeg source checkout is missing. Initialize submodules with "
        "'git submodule update --init --recursive'.")
endif()

if(NOT MINGW)
    message(FATAL_ERROR "The vendored FFmpeg source build currently requires the MinGW toolchain")
endif()

if(NOT XY_FFMPEG_BASH_EXECUTABLE)
    find_program(XY_GIT_EXECUTABLE NAMES git.exe git)
    if(XY_GIT_EXECUTABLE)
        get_filename_component(_xy_git_cmd_dir "${XY_GIT_EXECUTABLE}" DIRECTORY)
        get_filename_component(_xy_git_root "${_xy_git_cmd_dir}/.." ABSOLUTE)
        foreach(_xy_bash_candidate
                "${_xy_git_root}/bin/bash.exe"
                "${_xy_git_root}/usr/bin/bash.exe")
            if(EXISTS "${_xy_bash_candidate}")
                set(XY_FFMPEG_BASH_EXECUTABLE "${_xy_bash_candidate}" CACHE FILEPATH
                    "Git Bash executable used to build vendored FFmpeg")
                break()
            endif()
        endforeach()
    endif()
endif()
if(NOT XY_FFMPEG_BASH_EXECUTABLE)
    message(FATAL_ERROR
        "Git Bash is required to build vendored FFmpeg. Set XY_FFMPEG_BASH_EXECUTABLE "
        "to the Git for Windows bash.exe path.")
endif()

get_filename_component(_xy_ffmpeg_bash_dir "${XY_FFMPEG_BASH_EXECUTABLE}" DIRECTORY)
set(XY_FFMPEG_SH_EXECUTABLE "")
foreach(_xy_sh_candidate
        "${_xy_ffmpeg_bash_dir}/../usr/bin/sh.exe"
        "${_xy_ffmpeg_bash_dir}/sh.exe")
    get_filename_component(_xy_sh_candidate "${_xy_sh_candidate}" ABSOLUTE)
    if(EXISTS "${_xy_sh_candidate}")
        set(XY_FFMPEG_SH_EXECUTABLE "${_xy_sh_candidate}")
        break()
    endif()
endforeach()
if(NOT XY_FFMPEG_SH_EXECUTABLE)
    message(FATAL_ERROR
        "Git Bash's usr/bin/sh.exe is required by the FFmpeg Makefiles.")
endif()

function(xy_ffmpeg_msys_path INPUT_PATH OUTPUT_VARIABLE)
    file(TO_CMAKE_PATH "${INPUT_PATH}" _xy_path)
    if(_xy_path MATCHES "^([A-Za-z]):/(.*)$")
        string(TOLOWER "${CMAKE_MATCH_1}" _xy_drive)
        set(_xy_path "/${_xy_drive}/${CMAKE_MATCH_2}")
    endif()
    set(${OUTPUT_VARIABLE} "${_xy_path}" PARENT_SCOPE)
endfunction()

xy_ffmpeg_msys_path("${_xy_ffmpeg_source_dir}" _xy_ffmpeg_source_msys)
xy_ffmpeg_msys_path("${CMAKE_C_COMPILER}" _xy_ffmpeg_cc_msys)
xy_ffmpeg_msys_path("${CMAKE_CXX_COMPILER}" _xy_ffmpeg_cxx_msys)
xy_ffmpeg_msys_path("${CMAKE_MAKE_PROGRAM}" _xy_ffmpeg_make_msys)
file(TO_CMAKE_PATH "${XY_FFMPEG_SH_EXECUTABLE}" _xy_ffmpeg_sh_make)

ExternalProject_Add(XYFFmpegBuild
    SOURCE_DIR "${_xy_ffmpeg_source_dir}"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND
        "${XY_FFMPEG_BASH_EXECUTABLE}" -c
        "cd '${_xy_ffmpeg_source_msys}' && '${_xy_ffmpeg_source_msys}/configure' --cc='${_xy_ffmpeg_cc_msys}' --cxx='${_xy_ffmpeg_cxx_msys}' --arch=x86_64 --target-os=mingw64 --disable-autodetect --disable-network --disable-programs --disable-doc --disable-debug --disable-x86asm --disable-avdevice --disable-avfilter --disable-encoders --disable-muxers --disable-filters --enable-static --disable-shared"
    BUILD_COMMAND
        "${XY_FFMPEG_BASH_EXECUTABLE}" -c
        "cd '${_xy_ffmpeg_source_msys}' && '${_xy_ffmpeg_make_msys}' -j4 'SHELL=${_xy_ffmpeg_sh_make}'"
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS
        "${_xy_ffmpeg_source_dir}/libavformat/libavformat.a"
        "${_xy_ffmpeg_source_dir}/libavcodec/libavcodec.a"
        "${_xy_ffmpeg_source_dir}/libswscale/libswscale.a"
        "${_xy_ffmpeg_source_dir}/libswresample/libswresample.a"
        "${_xy_ffmpeg_source_dir}/libavutil/libavutil.a")

xy_module(XCore)
target_include_directories(XCore PUBLIC
    "${PROJECT_SOURCE_DIR}/vendor"
    "${_xy_ffmpeg_source_dir}")
target_link_libraries(XCore PUBLIC
    "${_xy_ffmpeg_source_dir}/libavformat/libavformat.a"
    "${_xy_ffmpeg_source_dir}/libavcodec/libavcodec.a"
    "${_xy_ffmpeg_source_dir}/libswscale/libswscale.a"
    "${_xy_ffmpeg_source_dir}/libswresample/libswresample.a"
    "${_xy_ffmpeg_source_dir}/libavutil/libavutil.a"
    m atomic ole32 uuid user32 bcrypt)
add_dependencies(XCore XYFFmpegBuild)

# ── 跨平台网络端点 ──
xy_module(Network)
if(WIN32)
    set_source_files_properties(
        "${PROJECT_SOURCE_DIR}/Modules/Network/src/Linux/SocketHandleLinux.cpp"
        PROPERTIES HEADER_FILE_ONLY TRUE)
    target_link_libraries(Network PUBLIC ws2_32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_source_files_properties(
        "${PROJECT_SOURCE_DIR}/Modules/Network/src/Windows/SocketHandleWindows.cpp"
        PROPERTIES HEADER_FILE_ONLY TRUE)
else()
    message(FATAL_ERROR "Network currently supports Windows and Linux only")
endif()
target_link_libraries(Network PUBLIC XCore)

# ── Media playback (control/state above XCore device and decoder services) ──
xy_module(Media)
target_link_libraries(Media PUBLIC XCore)

xy_module(Middle)


# # XMath：header-only（空壳 Policy + glm），INTERFACE 库，只提供 include 路径
# # 注意 vendor/glm 被拍平(glm.hpp 直接在 vendor/glm/ 下)，include 根是 vendor/
# add_library(XMath INTERFACE)
# target_include_directories(XMath INTERFACE
#     "${CMAKE_SOURCE_DIR}/Modules/XMath"
#     "${CMAKE_SOURCE_DIR}/Modules"
#     "${CMAKE_SOURCE_DIR}/vendor")

# ── Core 上层（有依赖）──
xy_module(DataStore)
target_link_libraries(DataStore PUBLIC XCore)

xy_module(Log)
target_link_libraries(Log PUBLIC DataStore)

# ── vendor：freetype(文字后端，per-pixel alpha 透明文字) ──
# add_subdirectory 源码编译，静态链接进 X_Y。关掉非必要外部依赖(zlib/png/svg…)。
set(FT_DISABLE_ZLIB TRUE)
set(FT_DISABLE_BZIP2 TRUE)
set(FT_DISABLE_PNG TRUE)
set(FT_DISABLE_HARFBUZZ TRUE)
set(FT_DISABLE_BROTLI TRUE)
add_subdirectory(vendor/freetype ${CMAKE_BINARY_DIR}/freetype EXCLUDE_FROM_ALL)

# 强制把 FreeType 的库输出到 dist/lib/mingw/
set_target_properties(freetype PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${XY_LIB_DIR}"
)

# ══════════════════════════════════════════════════════
# ── APP 层 ──
# 注：Application 已并入 Widget 模块（Widget/Application.h + src/Application.cpp）
xy_module(Movement)
target_link_libraries(Movement PUBLIC Log XCore)

xy_module(GraphicsContext)
target_link_libraries(GraphicsContext PUBLIC Log glad)

xy_module(Widget)
target_link_libraries(Widget PUBLIC Log  GraphicsContext XCore freetype)
if(WIN32)
    target_link_libraries(Widget PUBLIC ole32 shell32)
endif()

# ── UI（含 imgui，依赖 Widget/DataStore/Timer + opengl32）──
xy_module(UI)
target_link_libraries(UI PUBLIC Widget DataStore XCore Media)
if(WIN32)
    target_link_libraries(UI PUBLIC opengl32)
endif()
# imgui 源码并入 UI 模块
file(GLOB IMGUI_SRC "${PROJECT_SOURCE_DIR}/vendor/imgui/*.cpp")
list(APPEND IMGUI_SRC
    "${PROJECT_SOURCE_DIR}/vendor/imgui/backends/imgui_impl_win32.cpp"
    "${PROJECT_SOURCE_DIR}/vendor/imgui/backends/imgui_impl_opengl3.cpp")
target_sources(UI PRIVATE ${IMGUI_SRC})
target_include_directories(UI PUBLIC
    "${PROJECT_SOURCE_DIR}/vendor/imgui"
    "${PROJECT_SOURCE_DIR}/vendor/imgui/backends")

# ══════════════════════════════════════════════════════
# ── Render 层 ──
xy_module(Render)
target_link_libraries(Render PUBLIC Log Middle XCore glad)

xy_module(Model)
target_link_libraries(Model PUBLIC XCore)

# ── Physics：物理 + 碰撞（Pimpl 门面 + 可换后端），依赖自研 XMath ──
xy_module(Physics)
target_link_libraries(Physics PUBLIC XCore)

# ══════════════════════════════════════════════════════
# 仓库内工具与测试
include(CTest)
add_subdirectory(test "${PROJECT_SOURCE_DIR}/test/build")

# ══════════════════════════════════════════════════════
# 分发：把头文件安装到 dist/include（构建后执行，替代旧 MoveFiles.py）
# 用法：cmake --install build --prefix "D:/workbench/X_Y/dist"
#   或  cmake --install build --config Debug --prefix "D:/workbench/X_Y/dist"
# install(DIRECTORY) 自带递归，子目录里的 .h/.hpp 也会一并装进去
# ══════════════════════════════════════════════════════
install(DIRECTORY "${PROJECT_SOURCE_DIR}/Modules/"
        DESTINATION "include"
        FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp")
