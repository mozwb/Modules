#include "../ProcessPlatform.h"

#include <windows.h>

#include <string>
#include <vector>

namespace X_Y::Process::Detail
{
namespace
{
    std::wstring Widen(const std::string &utf8)
    {
        if (utf8.empty())
            return {};

        const int length = ::MultiByteToWideChar(
            CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        if (length <= 0)
            return {};

        std::wstring result(static_cast<std::size_t>(length), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                             result.data(), length);
        return result;
    }

    std::string Narrow(const std::wstring &wide)
    {
        if (wide.empty())
            return {};

        const int length = ::WideCharToMultiByte(
            CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
            nullptr, 0, nullptr, nullptr);
        if (length <= 0)
            return {};

        std::string result(static_cast<std::size_t>(length), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                             result.data(), length, nullptr, nullptr);
        return result;
    }

    // Windows 的 argv 加引号规则（和 CommandLineToArgvW 对得上）：
    // 含空格/制表/换行/引号才加引号；引号前的反斜杠要成对翻倍。
    std::wstring QuoteArgument(const std::wstring &argument)
    {
        if (argument.empty())
            return L"\"\"";

        bool needsQuotes = false;
        for (wchar_t c : argument)
        {
            if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\v' || c == L'"')
            {
                needsQuotes = true;
                break;
            }
        }
        if (!needsQuotes)
            return argument;

        std::wstring result;
        result.reserve(argument.size() + 2);
        result.push_back(L'"');

        std::size_t backslashes = 0;
        for (wchar_t c : argument)
        {
            if (c == L'\\')
            {
                ++backslashes;
                continue;
            }

            if (c == L'"')
            {
                result.append(backslashes * 2 + 1, L'\\');
                result.push_back(L'"');
                backslashes = 0;
                continue;
            }

            result.append(backslashes, L'\\');
            backslashes = 0;
            result.push_back(c);
        }

        result.append(backslashes * 2, L'\\');
        result.push_back(L'"');
        return result;
    }

    std::string LastErrorText(DWORD code)
    {
        LPWSTR buffer = nullptr;
        const DWORD length = ::FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

        std::wstring message;
        if (length > 0 && buffer != nullptr)
            message.assign(buffer, length);

        if (buffer != nullptr)
            ::LocalFree(buffer);

        while (!message.empty() &&
               (message.back() == L'\r' || message.back() == L'\n'))
            message.pop_back();

        if (message.empty())
            message = L"Windows 错误 " + std::to_wstring(code);

        return Narrow(message);
    }
} // namespace

Result RunPlatform(const std::string &program,
                   const std::vector<std::string> &arguments)
{
    std::wstring commandLine = QuoteArgument(Widen(program));

    for (const auto &argument : arguments)
    {
        commandLine += L' ';
        commandLine += QuoteArgument(Widen(argument));
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);

    PROCESS_INFORMATION process{};
    if (!::CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, 0,
                          nullptr, nullptr, &startup, &process))
    {
        return Result{false, -1, "启动失败: " + LastErrorText(::GetLastError())};
    }

    Result result;
    result.Started = true;

    const DWORD wait = ::WaitForSingleObject(process.hProcess, INFINITE);
    if (wait == WAIT_OBJECT_0)
    {
        DWORD exitCode = 0;
        if (::GetExitCodeProcess(process.hProcess, &exitCode))
            result.ExitCode = static_cast<int>(exitCode);
        else
            result.Error = "读取退出码失败: " + LastErrorText(::GetLastError());
    }
    else
    {
        result.ExitCode = -1;
        result.Error    = "等待进程失败: " + LastErrorText(::GetLastError());
    }

    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    return result;
}

} // namespace X_Y::Process::Detail
