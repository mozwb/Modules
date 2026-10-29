#include <Terminal/Process.h>

#include "ProcessPlatform.h"

namespace X_Y::Process
{
Result Run(std::string_view program, const std::vector<std::string> &arguments,
           Options options)
{
    if (program.empty())
        return Result{false, -1, "程序名为空"};

    return Detail::RunPlatform(std::string(program), arguments, options);
}

} // namespace X_Y::Process
