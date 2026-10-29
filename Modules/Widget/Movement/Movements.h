#pragma once

/*
Widget/Movement/Movements.h
version 0.0.0

具体事件类型定义。事件广播机制（XMovement / Dispatcher / Queue / Layer）
在 APP/EventBus.h；这里只保留「窗口和输入有哪些事件、每个事件带什么数据」。

广播链：

    Win32WndProc / 其他事件源
        -> APP EventBus.Queue
        -> LayerStack（先消费）
        -> EventDispatcher（Connect 的绑定）

依赖方向：Widget/Movement -> APP/EventBus。APP 不认识 Widget。
*/

#include "APP/EventBus.h"
#include "Core/Core.h"
#include "Log/XYLog.h"

#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>

namespace X_Y
{
    // ── 迁移期兼容别名：广播机制在 APP，类型名保持原样 ──────────────────────
    using XMovement = App::XMovement;
    using MovementSender = App::EventSender;
    using MovementReceiver = App::EventReceiver;
    using MovementHandler = App::EventHandler;
    using EventType = App::EventType;
    using TypeID = App::TypeID;
    using MovementBinding = App::EventBinding;
    using MovementDispatcher = App::EventDispatcher;
    using MovementQueue = App::EventQueue;
    using Layer = App::Layer;
    using LayerStack = App::LayerStack;

    // 窗口发送消息 -> Win32WndProc 转化成某种行为，放入消息队列
    // -> 分发器从队列中取出某种行为，调用所有监听该行为的函数。
    enum MovementCategory
    {
        None = 0,
        MTApplication = BIT(0),
        MTInput = BIT(1),
        MTKeyboard = BIT(2),
        MTMouse = BIT(3),
        MTMouseButton = BIT(4),
        MTWindow = BIT(5)
    };

    enum class MovementType
    {
        None = 0,
        AppTick,
        AppUpdate,
        AppRender,
        WindowClose,
        WindowResize,
        WindowFocus,
        WindowMoved,
        WindowDestroy,
        WindowPaint,
        KeyPressed,
        KeyReleased,
        KeyTyped,
        MouseButtonPressed,
        MouseButtonReleased,
        MouseMoved,
        MouseScrolled,
        WindowDragBegin,
        WindowDragEnd
    };

#define MOVEMENT_CLASS_TYPE(type)                                             \
    static MovementType GetStaticType() { return MovementType::type; }        \
    virtual MovementType GetType() const override { return GetStaticType(); } \
    virtual const char *GetName() const override { return #type; }

#define MOVEMENT_CLASS_CATEGORY(category) \
    virtual int GetCategoryFlags() const override { return category; }

    // 窗口的行为类型，携带自己的消息。窗口只负责把行为发送给消息队列。
    class Movement : public XMovement
    {
    public:
        virtual ~Movement() = default;

        Movement(MovementSender s) : XMovement(s) {}

        virtual MovementType GetType() const = 0;
        virtual const char *GetName() const = 0;
        virtual int GetCategoryFlags() const = 0;

        virtual std::string toString() const { return GetName(); }

        bool DispatchEvent(void *dispatcher) override
        {
            auto *d = static_cast<MovementDispatcher *>(dispatcher);
            return d->Dispatch(this->GetType(), this);
        }

        bool IsInCategory(MovementCategory category)
        {
            return GetCategoryFlags() & category;
        }

        friend std::ostream &operator<<(std::ostream &os, const Movement &e)
        {
            return os << e.toString();
        }
    };

    using EventSender = MovementSender;     // 发起者（窗口/任意对象）
    using EventReceiver = MovementReceiver; // 接收者（任意对象）

    // 用于逻辑事件。可以继续仿写队列和分发器。
    class Event : public XMovement
    {
    public:
        bool Handled = false;
        EventSender sender = nullptr;

        virtual ~Event() = default;

        Event(EventSender s) : XMovement(s) {}

        virtual int GetCategoryFlags() const = 0;
        virtual const char *GetName() const = 0;

        virtual std::string toString() const { return GetName(); }

        bool IsInCategory(MovementCategory category)
        {
            return GetCategoryFlags() & category;
        }

        friend std::ostream &operator<<(std::ostream &os, const Event &e)
        {
            return os << e.toString();
        }
    };

#define EVENT_DISPATHC_INIT                                      \
    bool DispatchEvent(void *dispatcher) override                \
    {                                                            \
        auto *d = static_cast<MovementDispatcher *>(dispatcher); \
        bool flag = d->Dispatch(this->GetType(), this);          \
        if (flag)                                                \
        {                                                        \
            XDEBUG("事件处理成功{}", this->GetName())            \
        }                                                        \
        else                                                     \
        {                                                        \
            XDEBUG("事件处理失败{}", this->GetName())            \
        }                                                        \
        return flag;                                             \
    }
}
