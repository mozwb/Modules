# Input —— 键盘/鼠标 + 音频输出。
#
# 平台：现只有 Windows 后端（src/Win32/KeyMapperWin32.cpp 消息钩子、
#       src/Audio/Win32/WasapiOutputDevice.cpp 走 WASAPI）。别的平台要新增
#       src/<Platform>/ 并在下面按平台挑选源文件 —— 这就是「跨平台写在各模块」的意思。
# 音频播放器 AudioPlayer.cpp 依赖 Decoder 的 Media，所以 FFmpeg 关掉时整块跳过。

if(NOT WIN32)
    message(STATUS "Input: 当前只有 Windows 后端（Win32 消息 + WASAPI），本平台跳过")
    return()
endif()

if(NOT XY_WITH_FFMPEG)
    message(STATUS "Input: AudioPlayer 依赖 Decoder/Media(FFmpeg)，XY_WITH_FFMPEG=OFF 时跳过")
    return()
endif()

if(NOT TARGET Decoder)
    message(STATUS "Input: 依赖 Decoder，而 Decoder 在本平台被跳过 → 一并跳过")
    return()
endif()

xy_module(Input DEPS Decoder)

# WASAPI / COM
target_link_libraries(Input PUBLIC ole32 uuid user32)
