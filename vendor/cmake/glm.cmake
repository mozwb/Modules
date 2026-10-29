# glm —— header-only。
set(_glm_root "${CMAKE_CURRENT_LIST_DIR}/../glm")
if(NOT EXISTS "${_glm_root}/glm.hpp")
    return()
endif()

if(NOT TARGET xy_vendor_glm)
    add_library(xy_vendor_glm INTERFACE)
    # 代码写 #include "glm/glm.hpp"，所以 include 根是 glm 的父目录。
    target_include_directories(xy_vendor_glm INTERFACE
        "${_glm_root}/..")
endif()

unset(_glm_root)
