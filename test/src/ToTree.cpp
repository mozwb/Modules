#include "XCore/FilesSystem/FilesSystem.h"
#include <iostream>

#ifdef XY_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

int main(int argc, char *argv[])
{
#ifdef XY_PLATFORM_WINDOWS
    // 树的边框字符是 UTF-8，控制台默认 CP936 时会显示为乱码
    SetConsoleOutputCP(CP_UTF8);
#endif

    std::string rootName = (argc < 2) ? "" : argv[1];
    X_Y::XPath inputPath =
        (argc < 2) ? X_Y::XPath::CurrentDirectory() : X_Y::XPath(argv[1]);

    std::cout << inputPath.ToTree(rootName) << std::endl;
}