#pragma once

// Process 内部平台接口。不进公开面：<windows.h> / <sys/wait.h> 只在平台实现里。

#include <Terminal/Process.h>

namespace X_Y::Process::Detail
{
    Result RunPlatform(const std::string &program,
                       const std::vector<std::string> &arguments);
}
