#include "../ProcessPlatform.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <exception>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace X_Y::Process::Detail
{
namespace
{
    bool CreatePipeCloexec(int fds[2], std::string &error)
    {
        if (::pipe(fds) != 0)
        {
            error = "pipe 失败: " + std::string(std::strerror(errno));
            fds[0] = -1;
            fds[1] = -1;
            return false;
        }

        for (int i = 0; i < 2; ++i)
        {
            if (::fcntl(fds[i], F_SETFD, FD_CLOEXEC) < 0)
            {
                const int code = errno;
                ::close(fds[0]);
                ::close(fds[1]);
                fds[0] = -1;
                fds[1] = -1;
                error = "fcntl 失败: " + std::string(std::strerror(code));
                return false;
            }
        }

        return true;
    }

    void ClosePipe(int fds[2])
    {
        if (fds[0] >= 0)
        {
            ::close(fds[0]);
            fds[0] = -1;
        }
        if (fds[1] >= 0)
        {
            ::close(fds[1]);
            fds[1] = -1;
        }
    }

    void ReadPipe(int fd, std::string &data, std::string &error)
    {
        char buffer[4096];

        for (;;)
        {
            const ssize_t count = ::read(fd, buffer, sizeof(buffer));
            if (count > 0)
            {
                data.append(buffer, static_cast<std::size_t>(count));
                continue;
            }

            if (count == 0)
                break;

            if (errno == EINTR)
                continue;

            error = "读取子进程输出失败: " + std::string(std::strerror(errno));
            break;
        }

        ::close(fd);
    }
} // namespace

Result RunPlatform(const std::string &program,
                   const std::vector<std::string> &arguments,
                   const Options &options)
{
    // 先把 argv 准备好，fork 之后只做 async-signal-safe 的事。
    std::vector<std::string> storage;
    storage.reserve(arguments.size() + 1);
    storage.push_back(program);
    for (const auto &argument : arguments)
        storage.push_back(argument);

    std::vector<char *> argv;
    argv.reserve(storage.size() + 1);
    for (auto &item : storage)
        argv.push_back(item.data());
    argv.push_back(nullptr);

    int errorPipe[2] = {-1, -1};
    int stdoutPipe[2] = {-1, -1};
    int stderrPipe[2] = {-1, -1};

    auto closeAll = [&]()
    {
        ClosePipe(errorPipe);
        ClosePipe(stdoutPipe);
        ClosePipe(stderrPipe);
    };

    std::string setupError;

    if (!CreatePipeCloexec(errorPipe, setupError))
        return Result{false, -1, setupError};

    if (options.CaptureStdout &&
        !CreatePipeCloexec(stdoutPipe, setupError))
    {
        closeAll();
        return Result{false, -1, "创建 stdout 管道失败: " + setupError};
    }

    if (options.CaptureStderr &&
        !CreatePipeCloexec(stderrPipe, setupError))
    {
        closeAll();
        return Result{false, -1, "创建 stderr 管道失败: " + setupError};
    }

    const pid_t pid = ::fork();
    if (pid < 0)
    {
        const int code = errno;
        closeAll();
        return Result{false, -1, "fork 失败: " + std::string(std::strerror(code))};
    }

    if (pid == 0)
    {
        ::close(errorPipe[0]);

        if (options.CaptureStdout)
        {
            if (::dup2(stdoutPipe[1], STDOUT_FILENO) < 0)
            {
                const int code = errno;
                const ssize_t ignored = ::write(errorPipe[1], &code, sizeof(code));
                (void)ignored;
                ::_exit(127);
            }
            ::close(stdoutPipe[0]);
            ::close(stdoutPipe[1]);
        }

        if (options.CaptureStderr)
        {
            if (::dup2(stderrPipe[1], STDERR_FILENO) < 0)
            {
                const int code = errno;
                const ssize_t ignored = ::write(errorPipe[1], &code, sizeof(code));
                (void)ignored;
                ::_exit(127);
            }
            ::close(stderrPipe[0]);
            ::close(stderrPipe[1]);
        }

        ::execvp(argv[0], argv.data());

        const int code = errno;
        const ssize_t ignored = ::write(errorPipe[1], &code, sizeof(code));
        (void)ignored;
        ::_exit(127);
    }

    ::close(errorPipe[1]);
    errorPipe[1] = -1;

    if (stdoutPipe[1] >= 0)
    {
        ::close(stdoutPipe[1]);
        stdoutPipe[1] = -1;
    }
    if (stderrPipe[1] >= 0)
    {
        ::close(stderrPipe[1]);
        stderrPipe[1] = -1;
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
            const int fd = stdoutPipe[0];
            outThread = std::thread([fd, &outData, &outError]()
            {
                ReadPipe(fd, outData, outError);
            });
            stdoutPipe[0] = -1;
        }

        if (options.CaptureStderr)
        {
            const int fd = stderrPipe[0];
            errThread = std::thread([fd, &errData, &errError]()
            {
                ReadPipe(fd, errData, errError);
            });
            stderrPipe[0] = -1;
        }
    }
    catch (const std::exception &exception)
    {
        if (stdoutPipe[0] >= 0)
        {
            ::close(stdoutPipe[0]);
            stdoutPipe[0] = -1;
        }
        if (stderrPipe[0] >= 0)
        {
            ::close(stderrPipe[0]);
            stderrPipe[0] = -1;
        }

        ::kill(pid, SIGKILL);
        int ignoredStatus = 0;
        while (::waitpid(pid, &ignoredStatus, 0) < 0 && errno == EINTR)
        {
        }

        if (outThread.joinable())
            outThread.join();
        if (errThread.joinable())
            errThread.join();

        closeAll();
        return Result{true, -1,
                      "创建读线程失败: " + std::string(exception.what())};
    }

    int     childErrno = 0;
    ssize_t readBytes  = 0;
    do
    {
        readBytes = ::read(errorPipe[0], &childErrno, sizeof(childErrno));
    } while (readBytes < 0 && errno == EINTR);

    ::close(errorPipe[0]);
    errorPipe[0] = -1;

    int   status = 0;
    pid_t waited = 0;
    int   waitError = 0;
    do
    {
        waited = ::waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);

    if (waited < 0)
        waitError = errno;

    if (outThread.joinable())
        outThread.join();
    if (errThread.joinable())
        errThread.join();

    Result result;
    result.StdOut = std::move(outData);
    result.StdErr = std::move(errData);

    if (waited < 0)
    {
        result.Started  = false;
        result.ExitCode = -1;
        result.Error    = "waitpid 失败: " + std::string(std::strerror(waitError));
    }
    else if (readBytes == static_cast<ssize_t>(sizeof(childErrno)))
    {
        result.Started  = false;
        result.ExitCode = 127;
        result.Error    = "启动失败: " + std::string(std::strerror(childErrno));
    }
    else
    {
        result.Started = true;

        if (WIFEXITED(status))
        {
            result.ExitCode = WEXITSTATUS(status);
        }
        else if (WIFSIGNALED(status))
        {
            result.ExitCode = 128 + WTERMSIG(status);
            result.Error    = "进程被信号 " + std::to_string(WTERMSIG(status)) + " 终止";
        }
        else
        {
            result.ExitCode = -1;
            result.Error    = "进程状态未知";
        }
    }

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

    return result;
}

} // namespace X_Y::Process::Detail
