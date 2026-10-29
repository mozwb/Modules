# ==============================================================================
# XYVendor.cmake —— 第三方依赖（只在**模块侧**需要，引擎内核不依赖 vendor）
#
# 目录约定：
#   <workspace>/vendor/<name>/CMakeLists.txt
#
# 探测到 vendor 下哪个子目录有 CMakeLists.txt，就把它 add_subdirectory 进来。
# 第三方自己负责创建目标（xy_vendor_glad / xy_vendor_glm / xy_vendor_stb /
# xy_vendor_ffmpeg / freetype / ...），模块只按 TARGET 是否存在决定用不用。
#
# 这样以后加第三方依赖只需要：
#   1. 把源码放进 vendor/<name>/
#   2. 在 vendor/<name>/ 下写 CMakeLists.txt
# 根 CMake 和 XYVendor.cmake 都不用再改。
# ==============================================================================
include_guard(GLOBAL)

# ── 定位 vendor 根 ───────────────────────────────────────────────────────────
set(XY_VENDOR_DIR "" CACHE PATH "第三方根目录（其下每个子目录可带 CMakeLists.txt）")

if(NOT XY_VENDOR_DIR)
    set(_xy_vendor_candidates
        "${PROJECT_SOURCE_DIR}/vendor"
        "${PROJECT_SOURCE_DIR}/Vendor"
        "${PROJECT_SOURCE_DIR}/../Modules/vendor"
        "${PROJECT_SOURCE_DIR}/../Modules/Vendor"
        "${PROJECT_SOURCE_DIR}/../vendor"
        "${PROJECT_SOURCE_DIR}/../Vendor")
    foreach(_cand IN LISTS _xy_vendor_candidates)
        if(EXISTS "${_cand}/glm" OR EXISTS "${_cand}/stb")
            set(XY_VENDOR_DIR "${_cand}")
            break()
        endif()
    endforeach()
endif()

set(XY_FFMPEG_ROOT "${XY_VENDOR_DIR}/FFmpeg" CACHE PATH "FFmpeg 根目录（含 libavcodec/ 等）")
option(XY_WITH_FFMPEG "允许构建依赖 FFmpeg 的模块" ON)


# xy_vendor_probe() —— 扫描 vendor/*/CMakeLists.txt 并接入。
function(xy_vendor_probe)
    if(NOT XY_VENDOR_DIR)
        message(STATUS "vendor: 未找到（引擎内核不依赖它；依赖第三方的模块会自动跳过）")
        return()
    endif()

    message(STATUS "vendor: ${XY_VENDOR_DIR}")

    # freetype 只用源码内置能力，关掉可选外部依赖，避免 configure 去找
    # zlib / png / harfbuzz / brotli。变量在 add_subdirectory 之前设置，
    # 子目录里的 if(FT_DISABLE_*) 能直接看到。
    set(FT_DISABLE_ZLIB TRUE)
    set(FT_DISABLE_BZIP2 TRUE)
    set(FT_DISABLE_PNG TRUE)
    set(FT_DISABLE_HARFBUZZ TRUE)
    set(FT_DISABLE_BROTLI TRUE)

    # 1) 第三方自带 CMakeLists.txt：直接 add_subdirectory。
    file(GLOB _xy_vendor_cmakes CONFIGURE_DEPENDS
        "${XY_VENDOR_DIR}/*/CMakeLists.txt")

    foreach(_cmake IN LISTS _xy_vendor_cmakes)
        get_filename_component(_dir "${_cmake}" DIRECTORY)
        get_filename_component(_name "${_dir}" NAME)

        message(STATUS "vendor: add ${_name}")
        add_subdirectory("${_dir}"
            "${CMAKE_BINARY_DIR}/vendor/${_name}"
            EXCLUDE_FROM_ALL)
    endforeach()

    # 2) 没有 CMakeLists.txt、但仓库里给了接线脚本的第三方：
    #    vendor/cmake/<name>.cmake。脚本自己判断源码在不在、建不建目标。
    #    子模块（imgui / stb / FFmpeg）不能往里面塞文件，否则父仓库追踪不到，
    #    所以它们的接线放这里，而不是 vendor/<name>/CMakeLists.txt。
    file(GLOB _xy_vendor_wrappers CONFIGURE_DEPENDS
        "${XY_VENDOR_DIR}/cmake/*.cmake")

    foreach(_wrapper IN LISTS _xy_vendor_wrappers)
        get_filename_component(_name "${_wrapper}" NAME_WE)
        message(STATUS "vendor: wire ${_name}")
        include("${_wrapper}")
    endforeach()

    if(NOT _xy_vendor_cmakes AND NOT _xy_vendor_wrappers)
        message(STATUS "vendor: 没有找到可接入的模块")
    endif()
endfunction()
