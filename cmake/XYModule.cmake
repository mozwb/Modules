# ==============================================================================
# XYModule.cmake —— 一个目录 = 一个模块 / 一个程序
#
# 目录约定：
#   Modules/<Name>/*.h           对外头（可含子目录，如 Math/Glm、Input/Audio）
#   Modules/<Name>/src/**.cpp    实现（可含子目录，如 Input/src/Audio/Win32）
#   Modules/<Name>/src/**.h      内部头，不进公开面
#
# 三类产物：
#   lib   xy_module(Foo)                    静态库  → out/lib/libFoo.a
#   dll   xy_module(Foo SHARED)             动态库  → out/bin/Foo.dll + out/lib/libFoo.dll.a
#   exe   xy_program(App DIR <dir> ...)     可执行  → out/bin/App.exe
#
# 全局默认产物类型由 XY_MODULE_LINKAGE 决定；模块 CMakeLists 可用 STATIC / SHARED 覆盖。
# 平台相关的链接库、源文件筛选写在**各模块自己的 CMakeLists.txt** 里，不在根写 if(WIN32)。
# ==============================================================================
include_guard(GLOBAL)

set(XY_MODULES_ROOT "${PROJECT_SOURCE_DIR}/Modules")

# ── 产物属性：把「跨平台差异」收在这一个函数里 ────────────────────────────────
function(xy_apply_artifact_props TARGET KIND)
    if(KIND STREQUAL "STATIC")
        set_target_properties(${TARGET} PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY "${XY_LIB_OUT_DIR}")

    elseif(KIND STREQUAL "SHARED")
        set_target_properties(${TARGET} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${XY_BIN_OUT_DIR}"
            ARCHIVE_OUTPUT_DIRECTORY "${XY_LIB_OUT_DIR}")

        if(WIN32)
            # 产物名就是身份：不要 libFoo.dll
            set_target_properties(${TARGET} PROPERTIES PREFIX "")
            # MinGW / clang 的 ld 默认 auto-export 所有全局符号；模块应当只导出入口
            if(XY_STRICT_EXPORTS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
                target_link_options(${TARGET} PRIVATE "-Wl,--exclude-all-symbols")
            endif()
        elseif(UNIX AND XY_STRICT_EXPORTS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${TARGET} PRIVATE
                -fvisibility=hidden -fvisibility-inlines-hidden)
        endif()

    elseif(KIND STREQUAL "EXECUTABLE")
        set_target_properties(${TARGET} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${XY_BIN_OUT_DIR}")

    else()
        message(FATAL_ERROR "xy_apply_artifact_props: 未知产物类型 '${KIND}'")
    endif()
endfunction()


# ── 模块：lib 或 dll ─────────────────────────────────────────────────────────
# xy_module(<Name> [HEADER_ONLY] [STATIC] [SHARED] [DIR <dir>]
#           [DEPS <target>...] [EXCLUDE <regex>...])
#   DIR 默认 Modules/<Name>；程序 DLL 可以用 DIR 指到 Programs/<Name>
function(xy_module NAME)
    cmake_parse_arguments(M "HEADER_ONLY;STATIC;SHARED" "DIR" "DEPS;EXCLUDE" ${ARGN})

    set(_dir "${M_DIR}")
    if(NOT _dir)
        set(_dir "${XY_MODULES_ROOT}/${NAME}")
    endif()
    if(NOT IS_DIRECTORY "${_dir}")
        message(FATAL_ERROR "xy_module(${NAME}): 模块目录不存在 -> ${_dir}")
    endif()

    # 依赖必须先声明（根按依赖顺序 add_subdirectory）
    foreach(_dep IN LISTS M_DEPS)
        if(NOT TARGET ${_dep})
            message(FATAL_ERROR
                "xy_module(${NAME}): 依赖 '${_dep}' 还不存在。"
                "根 CMakeLists 里的模块顺序必须是「依赖在前」。")
        endif()
    endforeach()

    # 产物类型：显式覆盖 > 全局默认
    if(M_HEADER_ONLY)
        set(_kind "INTERFACE")
    else()
        if(M_STATIC AND M_SHARED)
            message(FATAL_ERROR "xy_module(${NAME}): STATIC 与 SHARED 不能同时给")
        elseif(M_STATIC)
            set(_kind "STATIC")
        elseif(M_SHARED)
            set(_kind "SHARED")
        else()
            set(_kind "${XY_MODULE_LINKAGE}")
        endif()
    endif()

    # 公开面 = 模块根下除 src/ 以外的一切头
    file(GLOB_RECURSE _public CONFIGURE_DEPENDS
        "${_dir}/*.h" "${_dir}/*.hpp" "${_dir}/*.inl")
    list(FILTER _public EXCLUDE REGEX "/src/")

    # dll 里静态链进来的库，全局状态会各存一份 —— 提前叫出来
    if(_kind STREQUAL "SHARED")
        foreach(_dep IN LISTS M_DEPS)
            get_target_property(_dk ${_dep} XY_ARTIFACT_KIND)
            if(_dk STREQUAL "STATIC")
                message(WARNING
                    "xy_module(${NAME}): dll 静态链了静态库 '${_dep}'；"
                    "它的全局单例/分配器会在每个 dll 里各有一份。"
                    "这类库应改为进 engine（builtin）并通过 host 服务访问。")
            endif()
        endforeach()
    endif()

    if(_kind STREQUAL "INTERFACE")
        add_library(${NAME} INTERFACE)
        target_link_libraries(${NAME} INTERFACE ${M_DEPS})
        target_include_directories(${NAME} INTERFACE
            "${_dir}" "${XY_MODULES_ROOT}")
    else()
        file(GLOB_RECURSE _src CONFIGURE_DEPENDS
            "${_dir}/src/*.cpp" "${_dir}/src/*.c")
        foreach(_ex IN LISTS M_EXCLUDE)
            list(FILTER _src EXCLUDE REGEX "${_ex}")
        endforeach()
        if(NOT _src)
            message(FATAL_ERROR
                "xy_module(${NAME}): ${_dir}/src 下没有源文件"
                "（平台筛选后为空？本平台请用 return() 显式跳过）")
        endif()

        add_library(${NAME} ${_kind} ${_src} ${_public})
        target_link_libraries(${NAME} PUBLIC ${M_DEPS})
        target_include_directories(${NAME} PUBLIC
            "${_dir}" "${XY_MODULES_ROOT}")
        xy_apply_artifact_props(${NAME} ${_kind})
    endif()

    set_target_properties(${NAME} PROPERTIES
        FOLDER "Modules"
        XY_PUBLIC_HEADERS "${_public}"
        XY_ARTIFACT_KIND "${_kind}"
        XY_MODULE_DEPS "${M_DEPS}")

    set_property(GLOBAL APPEND PROPERTY XY_DECLARED_MODULES ${NAME})
endfunction()


# ── 程序：exe ────────────────────────────────────────────────────────────────
# xy_program(<Name> [DIR <dir>] [DEPS <target>...] [EXPORTS])
#   DIR 默认 Programs/<Name>，源文件取 <dir>/src/**.cpp
#   EXPORTS = engine 那种「exe 也导出符号、供模块 dll 反向链接」的用法
function(xy_program NAME)
    cmake_parse_arguments(P "EXPORTS" "DIR" "DEPS" ${ARGN})

    set(_dir "${P_DIR}")
    if(NOT _dir)
        set(_dir "${PROJECT_SOURCE_DIR}/Programs/${NAME}")
    endif()

    file(GLOB_RECURSE _src CONFIGURE_DEPENDS
        "${_dir}/src/*.cpp" "${_dir}/src/*.c")
    if(NOT _src)
        message(FATAL_ERROR "xy_program(${NAME}): ${_dir}/src 下没有源文件")
    endif()

    foreach(_dep IN LISTS P_DEPS)
        if(NOT TARGET ${_dep})
            message(FATAL_ERROR "xy_program(${NAME}): 依赖 '${_dep}' 还不存在")
        endif()
    endforeach()

    add_executable(${NAME} ${_src})
    target_link_libraries(${NAME} PRIVATE ${P_DEPS})
    target_include_directories(${NAME} PRIVATE
        "${_dir}" "${XY_MODULES_ROOT}")

    if(P_EXPORTS)
        set_target_properties(${NAME} PROPERTIES ENABLE_EXPORTS TRUE)
    endif()

    xy_apply_artifact_props(${NAME} EXECUTABLE)
    set_target_properties(${NAME} PROPERTIES FOLDER "Programs")
endfunction()


# ── 依赖闭包（供 XY_MODULES 选择器使用）──────────────────────────────────────
function(xy_module_closure OUT)
    set(_queue ${ARGN})
    set(_seen "")
    while(_queue)
        list(POP_FRONT _queue _cur)
        if(NOT TARGET ${_cur})
            continue()
        endif()
        if(_cur IN_LIST _seen)
            continue()
        endif()
        list(APPEND _seen ${_cur})
        get_target_property(_deps ${_cur} XY_MODULE_DEPS)
        if(_deps)
            list(APPEND _queue ${_deps})
        endif()
    endwhile()
    set(${OUT} "${_seen}" PARENT_SCOPE)
endfunction()
