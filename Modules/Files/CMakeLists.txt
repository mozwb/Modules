# Files —— 文件系统 / 流 / 文件对话框。
#
# 平台：当前只有 Windows 后端（src/FilesSystem.cpp 用 <Windows.h> + <commdlg.h> 的
#       GetOpenFileName），所以非 Windows 直接跳过整个模块。
# 注意：它还借用 Decoder 的 GBK→UTF16 模板（只用头，不欠 Decoder 的符号）。

if(NOT WIN32)
    message(STATUS "Files: 当前只有 Windows 后端，本平台跳过")
    return()
endif()

xy_module(Files STATIC DEPS Memory)

target_link_libraries(Files PUBLIC comdlg32)
