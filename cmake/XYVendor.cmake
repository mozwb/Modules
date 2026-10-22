# ==============================================================================
# XYVendor.cmake —— 第三方依赖（只在**模块侧**需要，引擎内核不依赖 vendor）
#
# 目录布局（B 与你约定的形状）：
#   <workspace>/Vendor/   glm/ stb/ FFmpeg/ ...
#   <workspace>/Modules/  自己写的模块源码
#
# 引擎只做「探测」：找到就建接线目标，找不到就什么都不建、也不报错。
# 需要第三方的模块自己在 CMakeLists 里判断 TARGET 是否存在，不存在就跳过。
# 所以「引擎能不能 configure」与「vendor 在不在」彻底解耦。
# ==============================================================================
include_guard(GLOBAL)

# ── 探测 ─────────────────────────────────────────────────────────────────────
set(XY_VENDOR_DIR "" CACHE PATH "第三方根目录（含 glm/ 与 stb/）")

if(NOT XY_VENDOR_DIR)
    set(_xy_vendor_candidates
        "${PROJECT_SOURCE_DIR}/Vendor"                    # 本仓库内（大小写都认）
        "${PROJECT_SOURCE_DIR}/vendor"
        "${PROJECT_SOURCE_DIR}/../Modules/Vendor"         # 工具源码仓库
        "${PROJECT_SOURCE_DIR}/../Modules/vendor"
        "${PROJECT_SOURCE_DIR}/../Vendor")
    foreach(_cand IN LISTS _xy_vendor_candidates)
        if(EXISTS "${_cand}/glm" OR EXISTS "${_cand}/stb")
            set(XY_VENDOR_DIR "${_cand}")
            break()
        endif()
    endforeach()
endif()

# 模块里写的是 #include "vendor/stb/stb_image.h"，所以要 vendor 的父目录
set(XY_VENDOR_PARENT "")
if(XY_VENDOR_DIR)
    get_filename_component(XY_VENDOR_PARENT "${XY_VENDOR_DIR}" DIRECTORY)
endif()

set(XY_FFMPEG_ROOT "${XY_VENDOR_DIR}/FFmpeg" CACHE PATH "FFmpeg 根目录（含 libavcodec/ 等）")
option(XY_WITH_FFMPEG "允许构建依赖 FFmpeg 的模块" ON)


# xy_vendor_probe() —— 建可见的接线目标，只是「有就建」，绝不 FATAL。
function(xy_vendor_probe)
    if(NOT XY_VENDOR_DIR)
        message(STATUS "vendor: 未找到（引擎内核不依赖它；依赖第三方的模块会自动跳过）")
        return()
    endif()
    message(STATUS "vendor: ${XY_VENDOR_DIR}")

    # glm（header-only）
    if(EXISTS "${XY_VENDOR_DIR}/glm" AND NOT TARGET xy_vendor_glm)
        add_library(xy_vendor_glm INTERFACE)
        target_include_directories(xy_vendor_glm INTERFACE "${XY_VENDOR_DIR}")
    endif()

    # stb（header-only）
    if(EXISTS "${XY_VENDOR_DIR}/stb" AND NOT TARGET xy_vendor_stb)
        add_library(xy_vendor_stb INTERFACE)
        target_include_directories(xy_vendor_stb INTERFACE "${XY_VENDOR_PARENT}")
    endif()

    # FFmpeg（静态库 / pkg-config）
    if(TARGET xy_vendor_ffmpeg)
        return()
    endif()
    if(NOT XY_WITH_FFMPEG)
        message(STATUS "vendor: XY_WITH_FFMPEG=OFF，跳过 FFmpeg")
        return()
    endif()

    set(_components avformat avcodec swscale swresample avutil)
    set(_ff_libs "")
    foreach(_name IN LISTS _components)
        foreach(_suffix a lib)
            set(_cand "${XY_FFMPEG_ROOT}/lib${_name}/lib${_name}.${_suffix}")
            if(EXISTS "${_cand}")
                list(APPEND _ff_libs "${_cand}")
                break()
            endif()
        endforeach()
    endforeach()

    if(_ff_libs)
        add_library(xy_vendor_ffmpeg INTERFACE)
        target_include_directories(xy_vendor_ffmpeg INTERFACE "${XY_FFMPEG_ROOT}")
        target_link_libraries(xy_vendor_ffmpeg INTERFACE ${_ff_libs})
        if(NOT WIN32)
            target_link_libraries(xy_vendor_ffmpeg INTERFACE m atomic)
        endif()
        message(STATUS "vendor: FFmpeg 静态库 ${_ff_libs}")

    else()
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(XY_FFMPEG QUIET IMPORTED_TARGET
                libavformat libavcodec libswscale libswresample libavutil)
        endif()
        if(TARGET PkgConfig::XY_FFMPEG)
            add_library(xy_vendor_ffmpeg INTERFACE)
            target_link_libraries(xy_vendor_ffmpeg INTERFACE PkgConfig::XY_FFMPEG)
            message(STATUS "vendor: FFmpeg 经 pkg-config")
        else()
            message(STATUS "vendor: 找不到 FFmpeg，依赖它的模块会自动跳过")
        endif()
    endif()
endfunction()
