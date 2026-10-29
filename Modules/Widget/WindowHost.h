#pragma once

/*
Widget/WindowHost.h
version 0.0.0

窗口生命周期宿主。住在 Widget 层，只向上依赖 APP，不反向。

为什么不是 Application：
  Application 只该管 Loop + EventBus，不能认识 XWidget。
  但“关掉主窗口就退出”“程序结束销毁还活着的托管窗口”确实是 Widget
  的语义，所以放在这里。

依赖方向：
  Widget/WindowHost  ->  APP/Application
  APP/*              -/-> Widget/*

WindowHost 用 App::Application 的 dispatcher 绑 WindowClose / WindowDestroy，
用它的延迟回收队列处理 XWidget 自毁，用它的 pushEvents / ProcessEvents
在 Shutdown 时把销毁消息泵完。
*/

#include <vector>

namespace X_Y
{
    class XWidget;

    class WindowHost
    {
    public:
        static WindowHost &Instance();

        WindowHost(const WindowHost &) = delete;
        WindowHost &operator=(const WindowHost &) = delete;

        // 显式指定“关掉它就退出程序”的窗口。nullptr = 取消主窗口。
        void SetMainWindow(XWidget *widget);
        XWidget *GetMainWindow() const { return m_MainWindow; }

        // 托管：程序退出时若还活着，Shutdown 会 destroy + 回收。
        void Own(XWidget *widget);
        void ForgetOwnedWindow(XWidget *widget);

        // 主循环结束后调用：销毁托管窗口，泵消息把销毁/回收走完。
        void Shutdown();

    private:
        WindowHost() = default;

        void OnMainWindowClose();
        void OnMainWindowDestroyed();

        static constexpr int kMaxShutdownRounds = 100000;

        XWidget *m_MainWindow = nullptr;
        std::vector<XWidget *> m_OwnedWindows;
    };
}
