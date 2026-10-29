// AppDemo 载入的假微程序。签名与 X_Y 的 xy_program_v1 一致：
//   int entry(const void* host, int argc, const char* const* argv)
// 这里不链 xy_api.h，只验证 APP 的加载/调用链路。

#if defined(_WIN32)
#define DEMO_EXPORT __declspec(dllexport)
#else
#define DEMO_EXPORT __attribute__((visibility("default")))
#endif

extern "C" DEMO_EXPORT int xy_program_v1(const void *host, int argc,
                                         const char *const *argv)
{
    (void)host;
    (void)argv;

    // 用一个固定退出码让调用方确认“确实调到了 DLL 里的函数”。
    return argc > 0 ? 7 : 0;
}
