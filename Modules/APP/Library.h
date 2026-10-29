#pragma once

/*
APP/Library.h
version 0.0.0

动态库 RAII。只做三件事：打开、找符号、关闭。

Windows  LoadLibraryA / GetProcAddress / FreeLibrary
POSIX    dlopen(RTLD_NOW|RTLD_LOCAL) / dlsym / dlclose

不负责：
  入口符号的调用约定、host 表、ABI 检查 -> MicroProgram 或使用方
  模块注册/卸载顺序                        -> X_Y 的 ModuleRegistry

符号名与文件路径分离：文件负责定位，符号负责 ABI 演进。
*/

#include <string>

namespace X_Y::App
{
    class Library
    {
    public:
        Library() = default;
        ~Library();

        Library(const Library &) = delete;
        Library &operator=(const Library &) = delete;

        Library(Library &&other) noexcept;
        Library &operator=(Library &&other) noexcept;

        // 打开失败时 error（可空）写原因；已有句柄会先关掉。
        bool Open(const std::string &path, std::string *error = nullptr);
        void Close();

        bool IsOpen() const { return m_Handle != nullptr; }
        explicit operator bool() const { return IsOpen(); }

        // 找不到返回 nullptr。
        void *Symbol(const char *name) const;

        const std::string &Path() const { return m_Path; }

    private:
        void *m_Handle = nullptr;
        std::string m_Path;
    };
}
