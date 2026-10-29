# FFmpeg —— 这里不重编 FFmpeg，只把已有静态库接成 interface target。
#
# 目录形状：
#   vendor/FFmpeg/libavformat/libavformat.a
#   vendor/FFmpeg/libavcodec/libavcodec.a
#   ...
#
# 没找到静态库时退回 pkg-config；两者都没有就不建目标，
# 依赖 FFmpeg 的模块会自己跳过。

if(NOT XY_WITH_FFMPEG)
    return()
endif()

set(_ffmpeg_root "${CMAKE_CURRENT_LIST_DIR}/../FFmpeg")
set(_ffmpeg_libs "")
foreach(_name avformat avcodec swscale swresample avutil)
    foreach(_suffix a lib)
        set(_cand "${_ffmpeg_root}/lib${_name}/lib${_name}.${_suffix}")
        if(EXISTS "${_cand}")
            list(APPEND _ffmpeg_libs "${_cand}")
            break()
        endif()
    endforeach()
endforeach()

if(_ffmpeg_libs)
    if(NOT TARGET xy_vendor_ffmpeg)
        add_library(xy_vendor_ffmpeg INTERFACE)
        target_include_directories(xy_vendor_ffmpeg INTERFACE
            "${_ffmpeg_root}")
        target_link_libraries(xy_vendor_ffmpeg INTERFACE ${_ffmpeg_libs})
        if(NOT WIN32)
            target_link_libraries(xy_vendor_ffmpeg INTERFACE m atomic)
        endif()
    endif()
    message(STATUS "vendor: FFmpeg 静态库 ${_ffmpeg_libs}")
    unset(_ffmpeg_root)
    unset(_ffmpeg_libs)
    return()
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(XY_FFMPEG QUIET IMPORTED_TARGET
        libavformat libavcodec libswscale libswresample libavutil)
endif()

if(TARGET PkgConfig::XY_FFMPEG AND NOT TARGET xy_vendor_ffmpeg)
    add_library(xy_vendor_ffmpeg INTERFACE)
    target_link_libraries(xy_vendor_ffmpeg INTERFACE PkgConfig::XY_FFMPEG)
    message(STATUS "vendor: FFmpeg 经 pkg-config")
else()
    message(STATUS "vendor: 找不到 FFmpeg，依赖它的模块会自动跳过")
endif()

unset(_ffmpeg_root)
unset(_ffmpeg_libs)
