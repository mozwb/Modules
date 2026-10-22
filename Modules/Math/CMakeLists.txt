# Math —— header-only，引擎内建（builtin）。Glm/GLM.h 对外的就是 glm。
# 头文件没有产物，所以是 INTERFACE；谁用谁把 glm 的 include 一起带走。

xy_module(Math HEADER_ONLY)

if(TARGET xy_vendor_glm)
    target_link_libraries(Math INTERFACE xy_vendor_glm)
else()
    message(STATUS "Math: 找不到 vendor/glm，使用方会编译失败（引擎内核不依赖本模块）")
endif()
