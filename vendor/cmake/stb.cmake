# stb —— header-only。
set(_stb_root "${CMAKE_CURRENT_LIST_DIR}/../stb")
if(NOT EXISTS "${_stb_root}/stb_image.h")
    return()
endif()

if(NOT TARGET xy_vendor_stb)
    add_library(xy_vendor_stb INTERFACE)
    # 代码写 #include "vendor/stb/stb_image.h"，include 根是 vendor 的父目录。
    target_include_directories(xy_vendor_stb INTERFACE
        "${_stb_root}/../.."
        "${_stb_root}")
endif()

unset(_stb_root)
