# APP 的最小验收：Loop 注入 / EventBus 派发 / Library / MicroProgram。
#
# AppDemoProgram 是被 AppDemo 载入的假微程序（导出 xy_program_v1），
# 用来验证“DLL 里的程序能被 APP 载入并调用”这条链路。

xy_program(AppDemo DEPS APP)

add_library(AppDemoProgram SHARED plugin/Plugin.cpp)

if(WIN32)
    set(_app_demo_suffix ".dll")
else()
    set(_app_demo_suffix ".so")
endif()

set_target_properties(AppDemoProgram PROPERTIES
    PREFIX ""
    SUFFIX "${_app_demo_suffix}"
    RUNTIME_OUTPUT_DIRECTORY "${XY_BIN_OUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${XY_BIN_OUT_DIR}")

set_target_properties(AppDemoProgram PROPERTIES FOLDER "Programs")
