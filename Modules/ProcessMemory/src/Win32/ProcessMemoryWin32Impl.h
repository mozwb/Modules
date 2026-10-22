#pragma once
#ifdef XY_PLATFORM_WINDOWS
#include <windows.h>
#endif
#include "../ProcessMemoryImpl.h"
namespace X_Y
{
    class ProcessMemoryWin32Impl : public ProcessMemoryImpl
    {
    public:
        ProcessMemoryWin32Impl();
        ~ProcessMemoryWin32Impl() override;

        bool Attach(uint32_t pid) override;
        bool Attach(const std::string &processName) override;
        void Detach() override;
        bool IsAttached() const override;

        bool ReadRaw(uintptr_t address, Buffer *buffer, size_t bytes) override;
        bool WriteRaw(uintptr_t address, const Buffer *buffer, size_t bytes) override;

        uintptr_t GetModuleBase(const std::string &moduleName) override;
        uintptr_t PatternScan(uintptr_t startAddr, size_t scanRange, const char *pattern, const char *mask) override;
        std::string GetLastErrorString() const override;

    private:
        HANDLE m_hProcess = nullptr;
        uint32_t m_lastError = 0;
    };
} // namespace X_Y
