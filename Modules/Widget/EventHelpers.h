#pragma once

/*
Widget/EventHelpers.h
version 0.0.0

Widget 侧的事件绑定便利函数。只做一件事：把 Connect / connect / disConnect
转发给 App::Application 的 dispatcher。

为什么放在 Widget 而不是 APP：
  APP 只提供 EventBus 机制，不该为“成员函数指针绑定”这种使用习惯负责。
  UI 依赖 Widget，通过这个头拿到便利函数；UI 不应该直接 include APP。

依赖方向：Widget/EventHelpers -> APP/Application。APP 不认识 Widget/UI。
*/

#include "APP/Application.h"

#include <utility>

namespace X_Y
{
    template <typename EnumT>
    void Connect(App::EventSender sender, EnumT type,
                 App::EventReceiver receiver, App::EventHandler handler)
    {
        if (auto *app = App::Application::instance())
        {
            app->GetDispatcher().Connect<EnumT>(sender, type, receiver,
                                                std::move(handler));
        }
    }

    template <typename EnumT>
    void disConnect(App::EventSender sender, EnumT type,
                    App::EventReceiver receiver)
    {
        if (auto *app = App::Application::instance())
        {
            app->GetDispatcher().disConnect<EnumT>(sender, type, receiver);
        }
    }

    inline void disConnect(App::EventSender sender, App::EventReceiver receiver)
    {
        if (auto *app = App::Application::instance())
        {
            app->GetDispatcher().disConnect(sender, receiver);
        }
    }

    inline void disConnect(App::EventReceiver receiver)
    {
        if (auto *app = App::Application::instance())
        {
            app->GetDispatcher().disConnect(receiver);
        }
    }

    template <typename S, typename E, typename R>
    void connect(S *sender, E type, R *receiver,
                 void (R::*slotFunc)(const App::XMovement &))
    {
        App::EventHandler handler = [receiver, slotFunc](const App::XMovement &e)
        {
            (receiver->*slotFunc)(e);
        };
        Connect(sender, type, receiver, std::move(handler));
    }

    template <typename S, typename E, typename R>
    void connect(S *sender, E type, R *receiver,
                 void (R::*slotFunc)(App::XMovement *))
    {
        App::EventHandler handler = [receiver, slotFunc](const App::XMovement &e)
        {
            (receiver->*slotFunc)(const_cast<App::XMovement *>(&e));
        };
        Connect(sender, type, receiver, std::move(handler));
    }

    template <typename S, typename E, typename R>
    void connect(S *sender, E type, R *receiver, void (R::*slotFunc)())
    {
        App::EventHandler handler = [receiver, slotFunc](const App::XMovement &)
        {
            (receiver->*slotFunc)();
        };
        Connect(sender, type, receiver, std::move(handler));
    }
}
