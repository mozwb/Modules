#pragma once
#include "ProcessMemoryImpl.h"
#include "Core/Core.h"
namespace X_Y
{
    // 原本想做成工具静态类，但是它impl内部其实持有变量记录状态，
    // 所以对于多线程同时进行可能需要各自记录自己的状态否则每次还要先Detach\
    // 可以做成静态工具类的，大概得有资源共享或者根本不持有资源的特点吧

    class ProcessMemory
    {
        // 禁止拷贝，支持移动
        ProcessMemory(const ProcessMemory &) = delete;
        ProcessMemory &operator=(const ProcessMemory &) = delete;

        ProcessMemory(ProcessMemory &&) noexcept;
        ProcessMemory &operator=(ProcessMemory &&) noexcept;

        explicit ProcessMemory();
        ~ProcessMemory();

        bool Attach(uint32_t pid);
        bool Attach(const std::string &processName);
        void Detach();
        bool IsAttached() const;

        /// @brief 读取远程内存到Buffer
        /// @param address 远程进程虚拟地址
        /// @param buffer 输出Buffer，内部会Ensure扩容
        /// @param bytes 需要读取的字节数
        bool ReadRaw(uintptr_t address, Buffer *buffer, size_t bytes);

        /// @brief 将Buffer数据写入远程内存
        /// @param address 远程进程虚拟地址
        /// @param buffer 源只读Buffer
        /// @param bytes 最多写入多少字节
        bool WriteRaw(uintptr_t address, const Buffer *buffer, size_t bytes);

        /// @brief 模板封装：直接读取单个POD类型
        template <typename T>
        bool Read(uintptr_t address, T &outValue)
        {
            Buffer tempBuf;
            if (!ReadRaw(address, &tempBuf, sizeof(T)))
                return false;
            outValue = tempBuf.Read<T>(0);
            return true;
        }

        /// @brief 模板封装：直接写入单个POD类型
        template <typename T>
        bool Write(uintptr_t address, const T &value)
        {
            Buffer tempBuf;
            tempBuf.Append(value);
            return WriteRaw(address, &tempBuf, sizeof(T));
        }

        uintptr_t GetModuleBase(const std::string &moduleName);
        uintptr_t PatternScan(uintptr_t startAddr, size_t scanRange, const char *pattern, const char *mask);
        std::string GetLastErrorString() const;

    private:
        Scope<ProcessMemoryImpl> Impl;
        friend ProcessMemory *CreateProcessMemory();
    };
    ProcessMemory *CreateProcessMemory();
    void DestroyProcessMemory(ProcessMemory *ptr);
} // namespace X_Y
