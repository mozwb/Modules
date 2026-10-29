# Input —— 控制台输入（全平台）+ 窗口键鼠 / 音频（Windows）。
#
# 拆成两个库，避免终端程序被音频/FFmpeg 拖下水：
#   InputConsole   控制台 stdin 按键，全平台，不依赖 Decoder。
#   Input          Windows 窗口/全局键鼠 + 音频；需要 Decoder/FFmpeg，非 Windows 跳过。
# 两个库的公开头都在本目录：Console.h 和 Input.h。

# ── 控制台输入：全平台 ──────────────────────────────────────────────────────
if(WIN32)
    set(_console_source "src/Console/Win32/ConsoleWin32.cpp")
else()
    set(_console_source "src/Console/Posix/ConsolePosix.cpp")
endif()

add_library(InputConsole STATIC ${_console_source})
target_link_libraries(InputConsole PUBLIC Core)
target_include_directories(InputConsole PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}" "${XY_MODULES_ROOT}")
xy_apply_artifact_props(InputConsole STATIC)
set_target_properties(InputConsole PROPERTIES
    FOLDER "Modules"
    XY_ARTIFACT_KIND "STATIC"
    XY_MODULE_DEPS "Core")
set_property(GLOBAL APPEND PROPERTY XY_DECLARED_MODULES InputConsole)

# ── 窗口 / 全局键鼠 + 音频：Windows ────────────────────────────────────────
if(NOT WIN32)
    message(STATUS "Input: 非 Windows，只编控制台输入 InputConsole")
    return()
endif()

if(NOT XY_WITH_FFMPEG)
    message(STATUS "Input: AudioPlayer 依赖 Decoder/Media(FFmpeg)，XY_WITH_FFMPEG=OFF 时只编 InputConsole")
    return()
endif()

if(NOT TARGET Decoder)
    message(STATUS "Input: 依赖 Decoder，而 Decoder 在本平台被跳过 → 只编 InputConsole")
    return()
endif()

xy_module(Input DEPS Decoder EXCLUDE "src/Console/")

# WASAPI / COM
target_link_libraries(Input PUBLIC ole32 uuid user32)
