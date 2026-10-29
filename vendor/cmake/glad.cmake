# glad —— OpenGL 函数加载器（源码静态库）。
set(_glad_root "${CMAKE_CURRENT_LIST_DIR}/../glad")
if(NOT EXISTS "${_glad_root}/src/glad.c")
    return()
endif()

if(NOT TARGET xy_vendor_glad)
    add_library(xy_vendor_glad STATIC "${_glad_root}/src/glad.c")
    target_include_directories(xy_vendor_glad PUBLIC
        "${_glad_root}/include")
    if(DEFINED XY_LIB_OUT_DIR)
        set_target_properties(xy_vendor_glad PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY "${XY_LIB_OUT_DIR}")
    endif()
endif()

unset(_glad_root)
