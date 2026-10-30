# IO —— 文件系统 + 读写字节流。
#
# 公开头两个：
#   Files.h    路径 / 目录 / 整文件读写 / 对话框（旧 Files 模块整个并进来了）
#   Stream.h   只读字节流接口 + 文件流 + 内存流
# 不叫 IO.h —— Windows 大小写不敏感，公开头叫 IO.h 会被系统的 <io.h> 撞上，
# 把 _dup2 / _fileno 的声明顶掉。
#
# <fstream> / <filesystem> / <Windows.h> 只出现在 src/*.cpp，不进公开面。

if(NOT WIN32)
    # FilesSystem.cpp 是 Win32 后端（文件对话框 + 借用 Decoder 的 GBK→UTF16 模板），
    # 其余流实现跨平台，所以非 Windows 只摘掉它。
    xy_module(IO STATIC DEPS Memory EXCLUDE "src/FilesSystem\\.cpp")
    return()
endif()

xy_module(IO STATIC DEPS Memory)

# 文件对话框（GetOpenFileName / GetSaveFileName）
target_link_libraries(IO PUBLIC comdlg32)
