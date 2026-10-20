# 独立构建这两个示例，不依赖根目录 CMake（根工程要求 MinGW + vendored FFmpeg）。
cmake_minimum_required(VERSION 3.16)
project(NetworkEcho LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 允许 CMakeLists.txt 位于仓库内任意层级的示例目录（samples/ 或 samples/NetworkEcho/ 等）：
# 向上找到包含 Modules/Network/src/Endpoints.cpp 的仓库根。
set(XY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(_xy_search "${XY_ROOT}")
while(NOT EXISTS "${_xy_search}/Modules/Network/src/Endpoints.cpp")
    get_filename_component(_xy_parent "${_xy_search}" DIRECTORY)
    if("${_xy_parent}" STREQUAL "${_xy_search}")
        message(FATAL_ERROR
            "Cannot locate the X_Y repository root (Modules/Network/src/Endpoints.cpp) "
            "above ${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    set(_xy_search "${_xy_parent}")
endwhile()
set(XY_ROOT "${_xy_search}")

# 只取示例真正用到的源码：
#   Network  : Endpoints / SocketAddr / NetworkError / 平台 SocketHandle
#   XCore    : Buffer 及其分配门面（Buffer.cpp 依赖 Memory::Alloc/Free）
add_library(xy_network_echo_deps STATIC
    "${XY_ROOT}/Modules/Network/src/Endpoints.cpp"
    "${XY_ROOT}/Modules/Network/src/SocketAddr.cpp"
    "${XY_ROOT}/Modules/Network/src/NetworkError.cpp"
    "${XY_ROOT}/Modules/XCore/src/Memory/Buffer.cpp"
    "${XY_ROOT}/Modules/XCore/src/Memory/XMemFacade.cpp"
    "${XY_ROOT}/Modules/XCore/src/Memory/XMemBackend.cpp"
    "${XY_ROOT}/Modules/XCore/src/Memory/XMemStats.cpp")

target_include_directories(xy_network_echo_deps PUBLIC
    "${XY_ROOT}/Modules") # 让 #include "Network/..."、"XCore/..." 都能解析

if(WIN32)
    # windows.h 会定义 min/max 宏，和 std::numeric_limits<...>::max() 冲突。
    target_compile_definitions(xy_network_echo_deps PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    target_sources(xy_network_echo_deps PRIVATE
        "${XY_ROOT}/Modules/Network/src/Windows/SocketHandleWindows.cpp")
    target_link_libraries(xy_network_echo_deps PUBLIC ws2_32)
elseif(UNIX)
    target_sources(xy_network_echo_deps PRIVATE
        "${XY_ROOT}/Modules/Network/src/Linux/SocketHandleLinux.cpp")
else()
    message(FATAL_ERROR "Network module currently supports Windows and Linux only")
endif()

foreach(app tcp_echo_server tcp_echo_client)
    add_executable(${app} ${app}.cpp)
    target_link_libraries(${app} PRIVATE xy_network_echo_deps)
endforeach()

if(MSVC)
    target_compile_options(xy_network_echo_deps PRIVATE /W4 /utf-8)
    target_compile_options(tcp_echo_server PRIVATE /W4 /utf-8)
    target_compile_options(tcp_echo_client PRIVATE /W4 /utf-8)
else()
    target_compile_options(xy_network_echo_deps PRIVATE -Wall -Wextra)
    target_compile_options(tcp_echo_server PRIVATE -Wall -Wextra)
    target_compile_options(tcp_echo_client PRIVATE -Wall -Wextra)
endif()
