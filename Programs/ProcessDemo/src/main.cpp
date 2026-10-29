#include <Terminal/Process.h>

#include <cstdio>
#include <string>
#include <vector>

namespace
{
    int Check(bool ok, const char *name)
    {
        std::printf("%-24s %s\n", name, ok ? "ok" : "FAIL");
        return ok ? 0 : 1;
    }
} // namespace

int main()
{
    using namespace X_Y;

#if defined(XY_PLATFORM_WINDOWS)
    const char *shell = "cmd.exe";
    const std::vector<std::string> echoArgs{"/c", "echo hello"};
    const std::vector<std::string> errorArgs{"/c", "echo oops 1>&2"};
    const std::vector<std::string> bothArgs{"/c", "echo out & echo err 1>&2"};
    const std::vector<std::string> exitArgs{"/c", "exit 7"};
    const std::vector<std::string> bigArgs{
        "/c", "for /L %i in (1,1,5000) do @echo x"};
#else
    const char *shell = "/bin/sh";
    const std::vector<std::string> echoArgs{"-c", "echo hello"};
    const std::vector<std::string> errorArgs{"-c", "echo oops >&2"};
    const std::vector<std::string> bothArgs{"-c", "echo out; echo err >&2"};
    const std::vector<std::string> exitArgs{"-c", "exit 7"};
    const std::vector<std::string> bigArgs{
        "-c", "for i in $(seq 1 5000); do echo x; done"};
#endif

    int failures = 0;

    Process::Options captureOut;
    captureOut.CaptureStdout = true;

    Process::Options captureErr;
    captureErr.CaptureStderr = true;

    Process::Options captureBoth;
    captureBoth.CaptureStdout = true;
    captureBoth.CaptureStderr = true;

    const auto out = Process::Run(shell, echoArgs, captureOut);
    failures += Check(out.Started && out.ExitCode == 0 &&
                          out.StdOut.find("hello") != std::string::npos,
                      "capture stdout");

    const auto err = Process::Run(shell, errorArgs, captureErr);
    failures += Check(err.Started && err.StdErr.find("oops") != std::string::npos,
                      "capture stderr");

    const auto both = Process::Run(shell, bothArgs, captureBoth);
    failures += Check(both.Started &&
                          both.StdOut.find("out") != std::string::npos &&
                          both.StdErr.find("err") != std::string::npos,
                      "capture both");

    const auto inherited = Process::Run(shell, echoArgs);
    failures += Check(inherited.Started && inherited.ExitCode == 0,
                      "no capture (inherited)");

    const auto exited = Process::Run(shell, exitArgs);
    failures += Check(exited.Started && exited.ExitCode == 7, "exit code 7");

    const auto big = Process::Run(shell, bigArgs, captureOut);
    failures += Check(big.Started && big.StdOut.size() > 4096, "large output");

    const auto missing = Process::Run("no-such-program-xyz-123", {});
    failures += Check(!missing.Started && !missing.Error.empty(),
                      "missing program");

    std::printf("failures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
