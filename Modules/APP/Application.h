#pragma once

/*
APP/Application.h
version 0.0.0

应用层内核：Loop + EventBus。

职责只有三件：
  1. 每轮先推进 Loop，再把队列里的事件派发完
  2. 管理运行标志和延迟回收安全点
  3. 暴露 dispatcher / queue 给使用方注册和投递

不负责：
  窗口、主窗口退出语义、托管窗口清理   -> Widget（下一阶段 WindowHost）
  平台窗口类注册 / DPI                 -> Widget 的入口或平台层
  命令解析 / 外部进程                  -> Terminal

延迟回收保留原来的语义：回收动作只登记，等本轮所有派发结束、没有栈帧再
持有事件 sender 时统一执行。窗口自毁的 UAF 保护不在这里丢。
*/

#include "APP/EventBus.h"
#include "APP/Loop.h"

#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace X_Y::App
{
    class Application
    {
    public:
        using PreDispatchHandler = std::function<bool(const XMovement &)>;

        explicit Application(std::unique_ptr<Loop> loop = nullptr);
        virtual ~Application();

        Application(const Application &) = delete;
        Application &operator=(const Application &) = delete;

        // 主循环：Pump 一次 -> 派发一轮 -> 再 Pump。
        void Run();
        void exec() { Run(); }

        // 请求退出：置运行标志，并通知 Loop。
        void Stop();
        void appClose() { Stop(); }

        bool IsRunning() const { return m_Running; }
        bool isRunning() const { return m_Running; }

        // 投递一个事件，所有权交给 Application（派发完 delete）。
        void Push(XMovement *event) { m_EventBus.Push(event); }
        void pushEvents(XMovement *event) { Push(event); }

        // 只泵一次平台消息（给退出清理这类需要单独转一轮的地方用）。
        void pushEvents();

        // 派发当前队列中的所有事件，末尾执行延迟回收。
        void ProcessEvents();

        // 延迟回收：只登记动作，安全点统一执行。
        void DeferRecycle(std::function<void()> action);
        void FlushDeferredRecycle();

        // 派发前的全局钩子；返回 true 表示已消费，跳过 dispatcher。
        void SetPreDispatchHandler(PreDispatchHandler handler)
        {
            m_PreDispatch = std::move(handler);
        }
        void ClearPreDispatchHandler() { m_PreDispatch = nullptr; }
        bool HasPreDispatchHandler() const { return static_cast<bool>(m_PreDispatch); }

        EventBus &GetEventBus() { return m_EventBus; }
        EventDispatcher &GetDispatcher() { return m_EventBus.Dispatcher(); }
        EventQueue &GetEventQueue() { return m_EventBus.Queue(); }
        Loop &GetLoop() { return *m_Loop; }

        static Application *instance() { return s_instance; }

    private:
        inline static Application *s_instance = nullptr;

        EventBus m_EventBus;
        std::unique_ptr<Loop> m_Loop;
        bool m_Running = true;

        PreDispatchHandler m_PreDispatch;
        std::vector<std::function<void()>> m_DeferredRecycle;
    };
}

