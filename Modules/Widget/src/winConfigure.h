#pragma once
#include <fcntl.h>
#ifdef XY_PLATFORM_WINDOWS
#include <iostream>
#include <Windows.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>

namespace X_Y {
    void allowConsole() {
        // 设置控制台编码为UTF-8，避免中文乱码
        SetConsoleCP(CP_UTF8);
        SetConsoleOutputCP(CP_UTF8);

        if (!::AttachConsole(ATTACH_PARENT_PROCESS)) {
            if (::AllocConsole()) {
                FILE* fp;
                // 重定向stdin
                if (freopen_s(&fp, "CONIN$", "r", stdin) == 0) {
                    _dup2(_fileno(stdin), 0);
                }
                // 重定向stdout
                if (freopen_s(&fp, "CONOUT$", "w", stdout) == 0) {
                    _dup2(_fileno(stdout), 1);
                }
                // 重定向stderr
                if (freopen_s(&fp, "CONOUT$", "w", stderr) == 0) {
                    _dup2(_fileno(stderr), 2);
                }
                // 同步C++流与C流
                std::ios::sync_with_stdio(true);
                std::cin.tie(nullptr);
                std::cout.tie(nullptr);
            }
        }

        // ========== 关键：开启控制台ANSI颜色支持 ==========
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut != INVALID_HANDLE_VALUE) {
            DWORD dwMode = 0;
            if (GetConsoleMode(hOut, &dwMode)) {
                // 启用虚拟终端处理，支持ANSI颜色
                dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
                SetConsoleMode(hOut, dwMode);
            }
        }

        // 同时给stderr也开启（可选，如果你用cerr输出日志）
        HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
        if (hErr != INVALID_HANDLE_VALUE) {
            DWORD dwMode = 0;
            if (GetConsoleMode(hErr, &dwMode)) {
                dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
                SetConsoleMode(hErr, dwMode);
            }
        }
    }
}
#endif  

