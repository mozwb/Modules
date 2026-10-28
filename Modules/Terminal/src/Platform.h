#pragma once

// Terminal 内部平台接口。不进公开面：<windows.h> / <termios.h> 只许出现在这里和
// src/Win32、src/Posix 的实现里。

#include <Terminal/Terminal.h>

namespace X_Y::Terminal::Detail
{
    // 现场探测，不改变任何状态。
    Caps ProbePlatform(Stream stream);

    // 打开该流需要的终端能力（Windows: VT + UTF-8；POSIX: 无操作）。
    // 返回开启后 ANSI 是否可用。
    bool EnablePlatform(Stream stream);

    // 还原 EnablePlatform 改过的全局状态。POSIX 空实现。
    void ShutdownPlatform();

    // 实时查窗口尺寸；拿不到返回 {0, 0}。
    Size GetSizePlatform(Stream stream);

    // stdin 和 stdout 都是终端。
    bool IsInteractivePlatform();
}
