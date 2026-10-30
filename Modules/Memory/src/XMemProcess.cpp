// 进程级内存：只有操作系统知道的那部分。
//
// 为什么单独一个文件：
//   Memory 其余部分都是纯标准库 + 自己的分配器，只有这里要碰平台 API。
//   把 <Windows.h> / <psapi.h> 关在这个 .cpp 里，公开头保持干净。
//
// 两个入口：
//   processMemory()        本进程（不用 OpenProcess，拿伪句柄就行）
//   processMemory(pid)     任意进程（Windows 要 OpenProcess，可能因权限失败）
//
// Windows：GetProcessMemoryInfo（psapi）
// Linux  ：/proc/<pid>/status + /proc/<pid>/smaps_rollup

#include <Memory/Memory.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef XY_PLATFORM_WINDOWS
#include <Windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#endif

namespace X_Y
{

    // ════════════════════════════════════════════════════════════════════════
    // pid
    // ════════════════════════════════════════════════════════════════════════

    uint32_t Memory::processId()
    {
        // 函数内 static：线程安全初始化，不碰堆，也就不可能绕回门面。
        static const uint32_t s_Pid = []
        {
#ifdef XY_PLATFORM_WINDOWS
            return static_cast<uint32_t>(::GetCurrentProcessId());
#else
            return static_cast<uint32_t>(::getpid());
#endif
        }();
        return s_Pid;
    }

    // ════════════════════════════════════════════════════════════════════════
    // 平台实现
    // ════════════════════════════════════════════════════════════════════════

#ifdef XY_PLATFORM_WINDOWS

    namespace
    {
        // 已经拿到进程句柄时的查询。伪句柄（GetCurrentProcess）和真句柄都能用。
        ProcessMemoryInfo QueryHandle(HANDLE process, uint32_t pid)
        {
            ProcessMemoryInfo info;
            info.ProcessId = pid;

            PROCESS_MEMORY_COUNTERS_EX counters = {};
            if (::GetProcessMemoryInfo(process,
                                       reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                                       static_cast<DWORD>(sizeof(counters))))
            {
                info.WorkingSetBytes = static_cast<uint64_t>(counters.WorkingSetSize);
                info.PeakWorkingSetBytes = static_cast<uint64_t>(counters.PeakWorkingSetSize);
                info.PrivateBytes = static_cast<uint64_t>(counters.PrivateUsage);
                info.Valid = true;
            }
            return info;
        }
    }

    ProcessMemoryInfo Memory::processMemory()
    {
        // 自己不用 OpenProcess：伪句柄永远有效，也不需要 CloseHandle。
        return QueryHandle(::GetCurrentProcess(), processId());
    }

    ProcessMemoryInfo Memory::processMemory(uint32_t pid)
    {
        if (pid == processId())
            return processMemory();

        ProcessMemoryInfo info;
        info.ProcessId = pid;

        // PROCESS_VM_READ 是为了 PrivateUsage；只查 RSS 的话 QUERY 权限就够。
        // 受限进程（系统进程、别的用户）会失败，那就 Valid = false。
        HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (process == nullptr)
            process = ::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (process == nullptr)
            return info;

        info = QueryHandle(process, pid);
        ::CloseHandle(process);
        return info;
    }

#else

    namespace
    {
        // 从 "VmRSS:  1234 kB" 这种行里取数字（kB）
        uint64_t ParseStatusKb(const char *line)
        {
            const char *colon = std::strchr(line, ':');
            if (colon == nullptr)
                return 0;
            return std::strtoull(colon + 1, nullptr, 10);
        }

        // 读 /proc/<pid>/ 下的两三个文件。进程不存在或没权限 → 打不开 → Valid = false。
        ProcessMemoryInfo QueryProc(uint32_t pid)
        {
            ProcessMemoryInfo info;
            info.ProcessId = pid;

            char path[64];

            std::snprintf(path, sizeof(path), "/proc/%u/status", pid);
            if (FILE *f = std::fopen(path, "r"))
            {
                char line[256];
                while (std::fgets(line, sizeof(line), f))
                {
                    if (std::strncmp(line, "VmRSS:", 6) == 0)
                        info.WorkingSetBytes = ParseStatusKb(line) * 1024;
                    else if (std::strncmp(line, "VmHWM:", 6) == 0)
                        info.PeakWorkingSetBytes = ParseStatusKb(line) * 1024;
                }
                std::fclose(f);
                info.Valid = info.WorkingSetBytes > 0;
            }

            // 私有内存 = Private_Clean + Private_Dirty（内核 4.14+ 才有 smaps_rollup）
            std::snprintf(path, sizeof(path), "/proc/%u/smaps_rollup", pid);
            if (FILE *f = std::fopen(path, "r"))
            {
                char line[256];
                uint64_t cleanKb = 0;
                uint64_t dirtyKb = 0;
                while (std::fgets(line, sizeof(line), f))
                {
                    if (std::strncmp(line, "Private_Clean:", 14) == 0)
                        cleanKb = ParseStatusKb(line);
                    else if (std::strncmp(line, "Private_Dirty:", 14) == 0)
                        dirtyKb = ParseStatusKb(line);
                }
                std::fclose(f);
                info.PrivateBytes = (cleanKb + dirtyKb) * 1024;
            }
            return info;
        }
    }

    ProcessMemoryInfo Memory::processMemory()
    {
        return QueryProc(processId());
    }

    ProcessMemoryInfo Memory::processMemory(uint32_t pid)
    {
        return pid == processId() ? processMemory() : QueryProc(pid);
    }

#endif

    // ════════════════════════════════════════════════════════════════════════
    // 打印
    // ════════════════════════════════════════════════════════════════════════

    std::string ProcessMemoryInfo::ToString() const
    {
        if (!Valid)
        {
            char failed[80];
            std::snprintf(failed, sizeof(failed), "[process %u] 查询失败（不存在或没权限）", ProcessId);
            return failed;
        }

        const double mb = 1024.0 * 1024.0;
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "[process %u] rss %.2f MB | peak %.2f MB | private %.2f MB",
                      ProcessId,
                      static_cast<double>(WorkingSetBytes) / mb,
                      static_cast<double>(PeakWorkingSetBytes) / mb,
                      static_cast<double>(PrivateBytes) / mb);
        return buf;
    }

} // namespace X_Y
