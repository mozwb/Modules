#include "Widget/WindowHost.h"

#include "APP/Application.h"
#include "Widget/EventHelpers.h"
#include "Widget/XWidget.h"

#include <utility>

namespace X_Y
{
    WindowHost &WindowHost::Instance()
    {
        static WindowHost host;
        return host;
    }

    void WindowHost::SetMainWindow(XWidget *widget)
    {
        if (m_MainWindow == widget)
        {
            return;
        }

        auto *app = App::Application::instance();

        // 解除旧主窗绑定，避免 dispatcher 里留下旧指针。
        if (m_MainWindow && app)
        {
            app->GetDispatcher().disConnect(static_cast<void *>(m_MainWindow),
                                            static_cast<void *>(this));
        }

        m_MainWindow = widget;

        if (widget && app)
        {
            connect(widget, MovementType::WindowClose, this,
                    &WindowHost::OnMainWindowClose);
            connect(widget, MovementType::WindowDestroy, this,
                    &WindowHost::OnMainWindowDestroyed);
        }
    }

    void WindowHost::OnMainWindowClose()
    {
        if (auto *app = App::Application::instance())
        {
            app->Stop();
        }
    }

    void WindowHost::OnMainWindowDestroyed()
    {
        m_MainWindow = nullptr;
    }

    void WindowHost::Own(XWidget *widget)
    {
        if (!widget)
        {
            return;
        }

        for (auto *owned : m_OwnedWindows)
        {
            if (owned == widget)
            {
                return;
            }
        }
        m_OwnedWindows.push_back(widget);
    }

    void WindowHost::ForgetOwnedWindow(XWidget *widget)
    {
        for (auto it = m_OwnedWindows.begin(); it != m_OwnedWindows.end(); ++it)
        {
            if (*it == widget)
            {
                m_OwnedWindows.erase(it);
                return;
            }
        }
    }

    void WindowHost::Shutdown()
    {
        auto *app = App::Application::instance();
        if (!app)
        {
            m_OwnedWindows.clear();
            m_MainWindow = nullptr;
            return;
        }

        app->Stop();

        // 从尾部取并先摘除：即使 destroy 没触发移除，循环也一定收敛。
        int guard = 0;
        while (!m_OwnedWindows.empty() && ++guard <= kMaxShutdownRounds)
        {
            XWidget *widget = m_OwnedWindows.back();
            m_OwnedWindows.pop_back();

            if (widget)
            {
                widget->destroy();
            }

            app->pushEvents();
            app->ProcessEvents();
        }

        app->FlushDeferredRecycle();
    }
}
