#pragma once
#include <cstdint>
#include <string>
#include "Memory/Memory.h"
namespace X_Y
{
    class ProcessMemoryImpl
    {
    public:
        virtual ~ProcessMemoryImpl() = default;
        virtual bool Attach(uint32_t pid) = 0;                   // 根据PID进行绑定
        virtual bool Attach(const std::string &processName) = 0; // 根据进程名进行绑定
        virtual void Detach() = 0;                               // 解绑某个PID
        virtual bool IsAttached() const = 0;                     // 判断是否绑定成功

        virtual bool ReadRaw(uintptr_t address, Buffer *buffer, size_t bytes) = 0;        // 读取某个地址的数据，基地址加偏移量
        virtual bool WriteRaw(uintptr_t address, const Buffer *buffer, size_t bytes) = 0; // 修改某个地址的数据
        virtual uintptr_t GetModuleBase(const std::string &moduleName) = 0;
        virtual uintptr_t PatternScan(uintptr_t startAddr, size_t scanRange, const char *pattern, const char *mask) = 0;
        virtual std::string GetLastErrorString() const = 0;
    };

    class ProcessMemoryFactory
    {
    public:
        static ProcessMemoryImpl *CreateProcessMemoryImpl();
    };
}