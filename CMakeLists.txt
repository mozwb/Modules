# ==============================================================================
# X_Y —— engine 根构建
#
# 根只做三件事：全局编译选项 / vendor 接线 / 按依赖顺序进入各目录。
# 「这个模块在 Windows 上链什么、在 Linux 上排哪个源文件」一律写在
# Modules/<Name>/CMakeLists.txt 里，根不再出现 if(WIN32)。
#
# 产物：
#   build/out/lib/*.a    静态库（xy_module）
#   build/out/lib/*.dll.a / *.lib   动态库的 import lib
#   build/out/bin/*.dll  动态库（xy_module(... SHARED) 或 -DXY_MODULE_LINKAGE=SHARED）
#   build/out/bin/*.exe  可执行（xy_program）
#
# 常用：
#   cmake -S . -B build -G Ninja
#   cmake --build build
#   cmake -S . -B build -DXY_MODULE_LINKAGE=SHARED     # 模块改出 dll
#   cmake -S . -B build -DXY_MODULES=Log              # 只编 Log 及其依赖
# ==============================================================================
cmake_minimum_required(VERSION 3.20)
project(X_Y LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Debug / Release" FORCE)
endif()

# ── 选项 ─────────────────────────────────────────────────────────────────────
# 模块默认产物类型：STATIC → lib，SHARED → dll。单个模块可在自己的 CMakeLists 里覆盖。
set(XY_MODULE_LINKAGE "STATIC" CACHE STRING "模块默认产物：STATIC(lib) 或 SHARED(dll)")
set_property(CACHE XY_MODULE_LINKAGE PROPERTY STRINGS STATIC SHARED)
if(NOT XY_MODULE_LINKAGE MATCHES "^(STATIC|SHARED)$")
    message(FATAL_ERROR "XY_MODULE_LINKAGE 只能是 STATIC 或 SHARED，当前 '${XY_MODULE_LINKAGE}'")
endif()

# dll 只导出显式标记的符号：MinGW 关 auto-export，Linux 用 -fvisibility=hidden
option(XY_STRICT_EXPORTS "dll 只导出显式标记的符号" ON)
option(XY_BUILD_TESTS "构建 test/" ON)
set(XY_MODULES "" CACHE STRING "只构建这些模块及其依赖（逗号或分号分隔，空=全部）")

# ── 输出目录 ─────────────────────────────────────────────────────────────────
set(XY_OUT_DIR     "${CMAKE_BINARY_DIR}/out")
set(XY_LIB_OUT_DIR "${XY_OUT_DIR}/lib")
set(XY_BIN_OUT_DIR "${XY_OUT_DIR}/bin")

# ── 平台宏 ───────────────────────────────────────────────────────────────────
if(WIN32)
    add_compile_definitions(XY_PLATFORM_WINDOWS UNICODE _UNICODE)
elseif(APPLE)
    add_compile_definitions(XY_PLATFORM_MAC)
else()
    add_compile_definitions(XY_PLATFORM_LINUX)
endif()

add_compile_definitions(
    $<$<CONFIG:Debug>:XY_DEBUG>
    $<$<CONFIG:Release>:XY_RELEASE>)

# ── 源码是 UTF-8（含中文注释），全链路保持一致 ──────────────────────────────
if(MSVC)
    add_compile_options(/utf-8)
else()
    add_compile_options(-finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

# ── cmake 层 ─────────────────────────────────────────────────────────────────
include(cmake/XYModule.cmake)
include(cmake/XYVendor.cmake)
xy_vendor_probe()

# ── 模块：**顺序 = 依赖顺序**（被依赖的在前）────────────────────────────────
set(XY_MODULE_ORDER
    Core
    Memory
    Timer
    Math
    Terminal
    Log
    Files
    DataStore
    Decoder
    Input)

foreach(_m IN LISTS XY_MODULE_ORDER)
    # 部分检出 / .gitignore 里的模块目录可能没有 CMakeLists.txt（比如 Modules/DataStore），
    # 没有就跳过，别让整个 configure 失败；依赖它的模块会自己用 TARGET 检查。
    if(EXISTS "${PROJECT_SOURCE_DIR}/Modules/${_m}/CMakeLists.txt")
        add_subdirectory("Modules/${_m}")
    else()
        message(STATUS "${_m}: 没有 CMakeLists.txt，跳过")
    endif()
endforeach()

# ── 可执行产物 ───────────────────────────────────────────────────────────────
# 构建器（xy）是引擎的一部分，不是外部工具，所以也放在 Engine/ 下，
# 和 X_Y.exe 共用 Engine/CMakeLists.txt。目录里有 CMakeLists.txt 才接进来。
if(EXISTS "${PROJECT_SOURCE_DIR}/Engine/CMakeLists.txt")
    add_subdirectory(Engine)
endif()

# ── 程序（被引擎载入的 DLL）──────────────────────────────────────────────────
# Programs/<Name>/CMakeLists.txt 一出现就自动接进来。
file(GLOB _xy_programs CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/Programs/*/CMakeLists.txt")
foreach(_p IN LISTS _xy_programs)
    get_filename_component(_pdir "${_p}" DIRECTORY)
    add_subdirectory("${_pdir}")
endforeach()

# ── 模块选择器：-DXY_MODULES=Log 只编这条链 ─────────────────────────────────
if(XY_MODULES)
    string(REPLACE "," ";" _xy_wanted "${XY_MODULES}")
    xy_module_closure(_xy_keep ${_xy_wanted})

    get_property(_xy_all GLOBAL PROPERTY XY_DECLARED_MODULES)
    foreach(_m IN LISTS _xy_all)
        if(NOT _m IN_LIST _xy_keep)
            set_target_properties(${_m} PROPERTIES EXCLUDE_FROM_ALL TRUE)
        endif()
    endforeach()
    message(STATUS "只构建: ${_xy_keep}")
endif()

# ── 测试 ─────────────────────────────────────────────────────────────────────
if(XY_BUILD_TESTS AND EXISTS "${PROJECT_SOURCE_DIR}/test/CMakeLists.txt")
    enable_testing()
    add_subdirectory(test)
endif()

# ── 配置摘要 ─────────────────────────────────────────────────────────────────
get_property(_xy_all GLOBAL PROPERTY XY_DECLARED_MODULES)
set(_xy_shared "")
set(_xy_static "")
set(_xy_interface "")
foreach(_m IN LISTS _xy_all)
    get_target_property(_k ${_m} XY_ARTIFACT_KIND)
    if(_k STREQUAL "SHARED")
        list(APPEND _xy_shared ${_m})
    elseif(_k STREQUAL "STATIC")
        list(APPEND _xy_static ${_m})
    else()
        list(APPEND _xy_interface ${_m})
    endif()
endforeach()
message(STATUS "产物 dll : ${_xy_shared}")
message(STATUS "产物 lib : ${_xy_static}")
message(STATUS "header   : ${_xy_interface}")
if(XY_MODULE_LINKAGE STREQUAL "SHARED" AND XY_STRICT_EXPORTS)
    message(STATUS
        "注意: dll 只导出显式 __declspec(dllexport) 的符号（XY_STRICT_EXPORTS=ON）。"
        "想让 ld 自动导出全部，加 -DXY_STRICT_EXPORTS=OFF。")
endif()
