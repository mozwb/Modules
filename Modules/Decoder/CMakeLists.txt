# Decoder —— 文本编码 / 图片(stb) / 媒体(FFmpeg)。
#
# 定位：**工具侧模块**（将来编成 dll 由引擎装载）。切 SHARED 前同样要先解决
# 内建符号来源，否则会把 Core/Memory/IO 各静态链一份进来。
#
# 平台：Decoder/Decoder.h（公开头）现在直接 include <windows.h>，非 Windows 编不了，
#       本平台先跳过；等把这个 include 从公开头挪走后就能跨过去。
# 第三方：stb 只取头（#include "vendor/stb/stb_image.h"），FFmpeg 由 xy_vendor_ffmpeg 提供；
#        vendor 找不到时本模块自动跳过，不影响引擎内核。

if(NOT WIN32)
    message(STATUS "Decoder: Decoder.h 直接 include <windows.h>，本平台跳过（待修）")
    return()
endif()

if(NOT TARGET IO)
    message(STATUS "Decoder: 依赖 IO，而 IO 在本平台被跳过 → 一并跳过")
    return()
endif()

# vendor 不在就跳过：引擎内核不需要 Decoder，不该因为第三方缺失而 configure 失败
if(NOT TARGET xy_vendor_stb)
    message(STATUS "Decoder: 找不到 vendor/stb，跳过")
    return()
endif()

if(XY_WITH_FFMPEG AND NOT TARGET xy_vendor_ffmpeg)
    message(STATUS "Decoder: 找不到 FFmpeg，Media 部分不参与构建")
    set(_have_ffmpeg OFF)
else()
    set(_have_ffmpeg ${XY_WITH_FFMPEG})
endif()

# XY_WITH_FFMPEG=OFF 时把 Media 实现摘掉，其余（文本/图片）照常
set(_exclude "")
if(NOT _have_ffmpeg)
    list(APPEND _exclude "src/Media\\.cpp$")
    message(STATUS "Decoder: 不含 FFmpeg，排除 src/Media.cpp")
endif()

xy_module(Decoder DEPS Core Memory IO EXCLUDE ${_exclude})

target_link_libraries(Decoder PRIVATE xy_vendor_stb)

if(_have_ffmpeg)
    target_link_libraries(Decoder PUBLIC xy_vendor_ffmpeg)
endif()

# 本模块代码自己用到的 Windows 系统库（avutil 的随机数种子走 advapi32/bcrypt）
target_link_libraries(Decoder PUBLIC bcrypt advapi32 ole32 uuid user32)
