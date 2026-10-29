# Media —— 播放器状态/线程；解码交给 Decoder，音频输出交给 Input。
#
# 只做机制：Open / Play / Pause / Stop / Seek / TakeVideoFrame。
# 不认识的平台后端由 Input / Decoder 自己决定。

if(NOT TARGET Decoder OR NOT TARGET Input)
    message(STATUS "Media: 缺少 Decoder 或 Input，跳过")
    return()
endif()

find_package(Threads REQUIRED)

xy_module(Media STATIC DEPS Core Decoder Input)

target_link_libraries(Media PUBLIC Threads::Threads)
