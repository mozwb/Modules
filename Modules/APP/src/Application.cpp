#include "APP/Application.h"

#include <cstdio>
#include <vector>

namespace X_Y::App
{
    Application::Application(std::unique_ptr<Loop> loop)
        : m_Loop(std::move(loop))
    {
        if (!s_instance)
        {
            s_instance = this;
        }

        if (!m_Loop)
        {
            m_Loop = CreatePlatformLoop();
        }

        if (m_Loop)
        {
            m_Loop->Boot();
        }
        else
        {
            std::fprintf(stderr,
                         "Application: 当前平台没有默认 Loop，只能手动 ProcessEvents\n");
        }
    }

    Application::~Application()
    {
        // 兜底：退出时可能还有没来得及回收的对象。
        FlushDeferredRecycle();

        if (s_instance == this)
        {
            s_instance = nullptr;
        }
    }

    void Application::Run()
    {
        while (m_Running)
        {
            if (m_Loop)
            {
                if (!m_Loop->Pump())
                {
                    m_Running = false;
                    break;
                }
            }

            ProcessEvents();

            // 没有 Loop 的裸环境：只派发一轮，不空转。
            if (!m_Loop)
            {
                break;
            }
        }
    }

    void Application::Stop()
    {
        m_Running = false;
        if (m_Loop)
        {
            m_Loop->Stop();
        }
    }

    void Application::pushEvents()
    {
        if (m_Loop)
        {
            m_Loop->Pump();
        }
    }

    void Application::ProcessEvents()
    {
        while (!m_EventBus.Queue().Empty())
        {
            XMovement *event = m_EventBus.Queue().Pop();
            if (!event)
            {
                continue;
            }

            for (auto *layer : m_EventBus.Layers().GetLayers())
            {
                layer->OnEvent(event);
                if (event->Handled)
                {
                    break;
                }
            }

            if (!event->Handled && m_PreDispatch)
            {
                if (m_PreDispatch(*event))
                {
                    event->Handled = true;
                }
            }

            if (!event->Handled)
            {
                event->DispatchEvent(static_cast<void *>(&m_EventBus.Dispatcher()));
            }

            delete event;
        }

        // 安全点：本轮事件全部派发完，所有回调栈帧都已返回。
        FlushDeferredRecycle();
    }

    void Application::DeferRecycle(std::function<void()> action)
    {
        if (action)
        {
            m_DeferredRecycle.push_back(std::move(action));
        }
    }

    void Application::FlushDeferredRecycle()
    {
        if (m_DeferredRecycle.empty())
        {
            return;
        }

        std::vector<std::function<void()>> actions;
        actions.swap(m_DeferredRecycle);

        for (auto &action : actions)
        {
            if (action)
            {
                action();
            }
        }
    }
}
