#include "../ProcessPlatform.h"

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

namespace X_Y::Process::Detail
{
Result RunPlatform(const std::string &program,
                   const std::vector<std::string> &arguments)
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
    if (::pipe(errorPipe) != 0)
        return Result{false, -1, "pipe 失败: " + std::string(std::strerror(errno))};

    // exec 成功时写端自动关闭，父进程读到 EOF；失败时子进程写 errno。
    if (::fcntl(errorPipe[1], F_SETFD, FD_CLOEXEC) < 0)
    {
        const int code = errno;
        ::close(errorPipe[0]);
        ::close(errorPipe[1]);
        return Result{false, -1, "fcntl 失败: " + std::string(std::strerror(code))};
    }

    const pid_t pid = ::fork();
    if (pid < 0)
    {
        const int code = errno;
        ::close(errorPipe[0]);
        ::close(errorPipe[1]);
        return Result{false, -1, "fork 失败: " + std::string(std::strerror(code))};
    }

    if (pid == 0)
    {
        ::close(errorPipe[0]);
        ::execvp(argv[0], argv.data());

        const int code = errno;
        const ssize_t ignored = ::write(errorPipe[1], &code, sizeof(code));
        (void)ignored;
        ::_exit(127);
    }

    ::close(errorPipe[1]);

    int     childErrno = 0;
    ssize_t readBytes  = 0;
    do
    {
        readBytes = ::read(errorPipe[0], &childErrno, sizeof(childErrno));
    } while (readBytes < 0 && errno == EINTR);

    ::close(errorPipe[0]);

    int   status = 0;
    pid_t waited = 0;
    do
    {
        waited = ::waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);

    if (waited < 0)
        return Result{false, -1, "waitpid 失败: " + std::string(std::strerror(errno))};

    if (readBytes == static_cast<ssize_t>(sizeof(childErrno)))
    {
        return Result{false, 127,
                      "启动失败: " + std::string(std::strerror(childErrno))};
    }

    Result result;
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

    return result;
}

} // namespace X_Y::Process::Detail
