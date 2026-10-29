#include "../ProcessPlatform.h"

#include <windows.h>

#include <cstddef>
#include <exception>
#include <string>
#include <thread>
#include <utility>
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

    bool CreateCapturePipe(HANDLE &readHandle, HANDLE &writeHandle,
                           std::string &error)
    {
        SECURITY_ATTRIBUTES attributes{};
        attributes.nLength        = sizeof(attributes);
        attributes.bInheritHandle = TRUE;

        if (!::CreatePipe(&readHandle, &writeHandle, &attributes, 0))
        {
            error = LastErrorText(::GetLastError());
            readHandle  = nullptr;
            writeHandle = nullptr;
            return false;
        }

        // 读端只给父进程，不能被子进程继承。
        if (!::SetHandleInformation(readHandle, HANDLE_FLAG_INHERIT, 0))
        {
            error = LastErrorText(::GetLastError());
            ::CloseHandle(readHandle);
            ::CloseHandle(writeHandle);
            readHandle  = nullptr;
            writeHandle = nullptr;
            return false;
        }

        return true;
    }

    HANDLE DuplicateForChild(HANDLE source, std::string &error)
    {
        if (source == nullptr || source == INVALID_HANDLE_VALUE)
            return nullptr;

        HANDLE duplicate = nullptr;
        if (!::DuplicateHandle(::GetCurrentProcess(), source, ::GetCurrentProcess(),
                               &duplicate, 0, TRUE, DUPLICATE_SAME_ACCESS))
        {
            error = "复制标准句柄失败: " + LastErrorText(::GetLastError());
            return nullptr;
        }

        return duplicate;
    }

    void ReadPipe(HANDLE pipe, std::string &data, std::string &error)
    {
        char  buffer[4096];

        for (;;)
        {
            DWORD count = 0;
            if (!::ReadFile(pipe, buffer, sizeof(buffer), &count, nullptr))
            {
                const DWORD code = ::GetLastError();
                if (code != ERROR_BROKEN_PIPE && code != ERROR_PIPE_NOT_CONNECTED &&
                    code != ERROR_OPERATION_ABORTED)
                    error = "读取子进程输出失败: " + LastErrorText(code);
                break;
            }

            if (count == 0)
                break;

            data.append(buffer, static_cast<std::size_t>(count));
        }

        ::CloseHandle(pipe);
    }
} // namespace

Result RunPlatform(const std::string &program,
                   const std::vector<std::string> &arguments,
                   const Options &options)
{
    std::wstring commandLine = QuoteArgument(Widen(program));

    for (const auto &argument : arguments)
    {
        commandLine += L' ';
        commandLine += QuoteArgument(Widen(argument));
    }

    HANDLE stdoutRead  = nullptr;
    HANDLE stdoutWrite = nullptr;
    HANDLE stderrRead  = nullptr;
    HANDLE stderrWrite = nullptr;
    HANDLE inputDup    = nullptr;
    HANDLE stdoutDup   = nullptr;
    HANDLE stderrDup   = nullptr;

    auto closeAll = [&]()
    {
        auto closeOne = [](HANDLE &handle)
        {
            if (handle != nullptr)
            {
                ::CloseHandle(handle);
                handle = nullptr;
            }
        };

        closeOne(stdoutRead);
        closeOne(stdoutWrite);
        closeOne(stderrRead);
        closeOne(stderrWrite);
        closeOne(inputDup);
        closeOne(stdoutDup);
        closeOne(stderrDup);
    };

    std::string setupError;

    if (options.CaptureStdout &&
        !CreateCapturePipe(stdoutRead, stdoutWrite, setupError))
    {
        closeAll();
        return Result{false, -1, "创建 stdout 管道失败: " + setupError};
    }

    if (options.CaptureStderr &&
        !CreateCapturePipe(stderrRead, stderrWrite, setupError))
    {
        closeAll();
        return Result{false, -1, "创建 stderr 管道失败: " + setupError};
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);

    const bool capturing = options.CaptureStdout || options.CaptureStderr;
    if (capturing)
    {
        startup.dwFlags |= STARTF_USESTDHANDLES;

        startup.hStdInput = DuplicateForChild(::GetStdHandle(STD_INPUT_HANDLE),
                                              setupError);
        if (!setupError.empty())
        {
            closeAll();
            return Result{false, -1, setupError};
        }
        inputDup = startup.hStdInput;

        if (options.CaptureStdout)
        {
            startup.hStdOutput = stdoutWrite;
        }
        else
        {
            stdoutDup = DuplicateForChild(::GetStdHandle(STD_OUTPUT_HANDLE),
                                          setupError);
            if (!setupError.empty())
            {
                closeAll();
                return Result{false, -1, setupError};
            }
            startup.hStdOutput = stdoutDup;
        }

        if (options.CaptureStderr)
        {
            startup.hStdError = stderrWrite;
        }
        else
        {
            stderrDup = DuplicateForChild(::GetStdHandle(STD_ERROR_HANDLE),
                                          setupError);
            if (!setupError.empty())
            {
                closeAll();
                return Result{false, -1, setupError};
            }
            startup.hStdError = stderrDup;
        }
    }

    PROCESS_INFORMATION process{};
    if (!::CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, 0,
                          nullptr, nullptr, &startup, &process))
    {
        const std::string message = LastErrorText(::GetLastError());
        closeAll();
        return Result{false, -1, "启动失败: " + message};
    }

    // 父进程不再需要写端和复制的标准句柄。
    if (stdoutWrite != nullptr)
    {
        ::CloseHandle(stdoutWrite);
        stdoutWrite = nullptr;
    }
    if (stderrWrite != nullptr)
    {
        ::CloseHandle(stderrWrite);
        stderrWrite = nullptr;
    }
    if (inputDup != nullptr)
    {
        ::CloseHandle(inputDup);
        inputDup = nullptr;
    }
    if (stdoutDup != nullptr)
    {
        ::CloseHandle(stdoutDup);
        stdoutDup = nullptr;
    }
    if (stderrDup != nullptr)
    {
        ::CloseHandle(stderrDup);
        stderrDup = nullptr;
    }

    std::string outData;
    std::string errData;
    std::string outError;
    std::string errError;

    std::thread outThread;
    std::thread errThread;

    try
    {
        if (options.CaptureStdout)
        {
            HANDLE handle = stdoutRead;
            outThread = std::thread([handle, &outData, &outError]()
            {
                ReadPipe(handle, outData, outError);
            });
            stdoutRead = nullptr; // 所有权交给线程
        }

        if (options.CaptureStderr)
        {
            HANDLE handle = stderrRead;
            errThread = std::thread([handle, &errData, &errError]()
            {
                ReadPipe(handle, errData, errError);
            });
            stderrRead = nullptr;
        }
    }
    catch (const std::exception &exception)
    {
        if (stdoutRead != nullptr)
            ::CloseHandle(stdoutRead);
        if (stderrRead != nullptr)
            ::CloseHandle(stderrRead);

        ::TerminateProcess(process.hProcess, 1);
        ::WaitForSingleObject(process.hProcess, INFINITE);

        if (outThread.joinable())
            outThread.join();
        if (errThread.joinable())
            errThread.join();

        ::CloseHandle(process.hThread);
        ::CloseHandle(process.hProcess);

        return Result{true, -1,
                      "创建读线程失败: " + std::string(exception.what())};
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
        const DWORD waitError = ::GetLastError();
        ::TerminateProcess(process.hProcess, 1);
        ::WaitForSingleObject(process.hProcess, INFINITE);
        result.ExitCode = -1;
        result.Error    = "等待进程失败: " + LastErrorText(waitError);
    }

    if (outThread.joinable())
        outThread.join();
    if (errThread.joinable())
        errThread.join();

    result.StdOut = std::move(outData);
    result.StdErr = std::move(errData);

    if (!outError.empty())
    {
        if (!result.Error.empty())
            result.Error += "; ";
        result.Error += outError;
    }
    if (!errError.empty())
    {
        if (!result.Error.empty())
            result.Error += "; ";
        result.Error += errError;
    }

    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    return result;
}

} // namespace X_Y::Process::Detail
