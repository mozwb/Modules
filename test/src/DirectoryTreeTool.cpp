#include "XCore/FilesSystem/FilesSystem.h"

#include <algorithm>
#include <iostream>
#include <string>

#ifdef XY_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace
{
    std::string ToDisplayRoot(const std::string &path)
    {
        std::string root = path;
        std::replace(root.begin(), root.end(), '\\', '/');
        if (!root.empty() && root.back() != '/')
            root += '/';
        return root;
    }

    bool RunSelfTest()
    {
        const std::string actual = X_Y::BuildTreeText(
            {
                "src/Memory/Buffer.cpp",
                "Network/b.cpp",
                "include/XCore/Memory/Buffer.h",
                "Network/a.h",
            },
            "X_Y/Modules/");

        const std::string expected =
            "X_Y/Modules/\n"
            "├─ Network/\n"
            "│  ├─ a.h\n"
            "│  └─ b.cpp\n"
            "├─ include/\n"
            "│  └─ XCore/\n"
            "│     └─ Memory/\n"
            "│        └─ Buffer.h\n"
            "└─ src/\n"
            "   └─ Memory/\n"
            "      └─ Buffer.cpp\n";

        if (actual == expected)
            return true;

        std::cerr << "[self-test] tree mismatch\n"
                  << "--- expected ---\n"
                  << expected
                  << "--- actual ---\n"
                  << actual;
        return false;
    }

    void PrintUsage()
    {
        std::cout << "Usage: XYDirectoryTreeTool [directory] [--out <file>]\n"
                  << "       XYDirectoryTreeTool --self-test\n";
    }
}

int main(int argc, char **argv)
{
#ifdef XY_PLATFORM_WINDOWS
    SetConsoleOutputCP(CP_UTF8);
#endif

    std::string target = "Modules";
    std::string output;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--self-test")
            return RunSelfTest() ? 0 : 1;
        if (arg == "--help" || arg == "-h")
        {
            PrintUsage();
            return 0;
        }
        if (arg == "--out")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "--out requires a file path\n";
                return 1;
            }
            output = argv[++i];
            continue;
        }
        target = arg;
    }

    X_Y::XPath root(target);
    if (!root.Exists() || !root.IsDirectory())
    {
        std::cerr << "Not a directory: " << target << "\n";
        return 1;
    }

    const std::string tree = root.ToTree(ToDisplayRoot(target));

    if (output.empty())
    {
        std::cout << tree;
        return 0;
    }

    X_Y::Buffer buffer(tree.size());
    buffer.WriteBytes(0, tree.data(), tree.size());
    if (!X_Y::FilesSystem::WriteFileBinary(output, buffer))
    {
        std::cerr << "Failed to write: " << output << "\n";
        return 1;
    }

    std::cout << "Tree written to " << output << "\n";
    return 0;
}
