#pragma once

/*
APP/Loop.h
version 0.0.0

一次循环推进的抽象。

Application 只依赖这个接口，不关心底下是 Win32 消息、终端读行还是以后
X_Y 的帧循环。默认实现由 CreatePlatformLoop() 提供；使用方也可以自己注入。

  Loop::Boot()   平台启动（只做平台初始化，不做窗口业务）
  Loop::Pump()   推进一次；返回 false 表示收到退出
  Loop::Stop()   请求退出；Application 每轮开始前也会看自己的运行标志

Windows 默认实现：PeekMessage / TranslateMessage / DispatchMessage。
POSIX 默认实现：留空转，等 X_Y 命令循环或具体 GUI 后端注入。
*/

#include <memory>

namespace X_Y::App
{
    class Loop
    {
    public:
        virtual ~Loop() = default;

        virtual void Boot() {}
        virtual bool Pump() = 0;
        virtual void Stop() {}
    };

    // 旧名字：迁移期先用着，以后统一叫 Loop。
    using PlatformLoop = Loop;

    // 当前平台的默认循环。没有平台实现时返回 nullptr。
    std::unique_ptr<Loop> CreatePlatformLoop();
}
