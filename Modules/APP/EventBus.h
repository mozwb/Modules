#pragma once

/*
APP/EventBus.h
version 0.0.0

事件广播机制。这里只有「怎么把事件送到监听者」，没有具体事件类型。

做

  XMovement         事件基类：sender / Handled / DispatchEvent
  EventDispatcher   绑定表 + Connect / disConnect / Dispatch
  EventQueue        待派发事件队列
  Layer / LayerStack 派发前的高优先级消费者
  EventBus          上面三者的组合，Application 持有它

不做

  具体事件（WindowClose / KeyPressed / ...） -> Widget/Movement
  循环、延迟回收                             -> Application
  窗口 / 平台                                -> Widget

约定

  . sender / receiver 都是 void*，跨窗口、跨模块不要求共同基类。
  . Connect 按「枚举类型 + 枚举值 + sender」匹配，不同类型即使数值相同也不串。
  . Dispatch 先拷贝命中的 handler 再执行，回调里增删绑定不会让遍历失效。
  . 队列只存裸指针，Pop 之后由 Application 负责 delete。
*/

#include "Core/Core.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <queue>
#include <type_traits>
#include <vector>

namespace X_Y::App
{
    using EventSender = void *;
    using EventReceiver = void *;

    // 所有事件的根。具体事件实现 DispatchEvent，把枚举类型交给 dispatcher。
    struct XMovement
    {
        virtual ~XMovement() = default;

        explicit XMovement(EventSender s) : sender(s) {}

        virtual bool DispatchEvent(void *dispatcher) = 0;

        EventSender sender = nullptr;
        bool Handled = false;
    };

    using EventHandler = std::function<void(const XMovement &)>;
    using EventType = std::uint32_t;
    using TypeID = std::uint32_t;

    struct EventBinding
    {
        EventSender sender = nullptr;
        EventType type = 0;
        TypeID typeId = 0;
        EventReceiver receiver = nullptr;
        EventHandler handler;
    };

    class EventDispatcher
    {
    public:
        template <typename EnumT>
        void Connect(EventSender sender, EnumT type, EventReceiver receiver,
                     EventHandler handler)
        {
            static_assert(std::is_enum_v<EnumT>, "必须是枚举类型");

            EventBinding bind{};
            bind.sender = sender;
            bind.type = static_cast<EventType>(type);
            bind.typeId = GetTypeId<EnumT>();
            bind.receiver = receiver;
            bind.handler = std::move(handler);

            m_Bindings.push_back(std::move(bind));
        }

        template <typename EnumT>
        void disConnect(EventSender sender, EnumT type, EventReceiver receiver)
        {
            static_assert(std::is_enum_v<EnumT>, "必须是枚举类型");

            const TypeID typeId = GetTypeId<EnumT>();
            const EventType typeVal = static_cast<EventType>(type);

            for (auto it = m_Bindings.begin(); it != m_Bindings.end();)
            {
                if (it->sender == sender && it->typeId == typeId &&
                    it->type == typeVal && it->receiver == receiver)
                {
                    it = m_Bindings.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        // 按 sender + receiver 删除。
        void disConnect(EventSender sender, EventReceiver receiver)
        {
            for (auto it = m_Bindings.begin(); it != m_Bindings.end();)
            {
                if (it->sender == sender && it->receiver == receiver)
                {
                    it = m_Bindings.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        // 删除和这个对象相关的所有订阅（sender 或 receiver 任一命中）。
        // 对象销毁前调一次，防止 dispatcher 里留下野指针。
        void disConnect(EventReceiver receiver)
        {
            for (auto it = m_Bindings.begin(); it != m_Bindings.end();)
            {
                if (it->sender == receiver || it->receiver == receiver)
                {
                    it = m_Bindings.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        bool DispatchEvent(std::uint32_t typeId, std::uint32_t typeVal,
                           XMovement *event)
        {
            if (!event)
            {
                return false;
            }

            // 先快照：回调里可能 Connect / disConnect，不能边遍历边改表。
            std::vector<EventHandler> pending;
            for (const auto &bind : m_Bindings)
            {
                if (bind.sender == event->sender && bind.typeId == typeId &&
                    bind.type == typeVal)
                {
                    pending.push_back(bind.handler);
                }
            }

            bool handled = false;
            for (auto &handler : pending)
            {
                if (handler)
                {
                    handler(*event);
                    handled = true;
                }
            }

            event->Handled = handled;
            return handled;
        }

        template <typename EnumT>
        bool Dispatch(EnumT type, XMovement *event)
        {
            return DispatchEvent(GetTypeId<EnumT>(),
                                 static_cast<std::uint32_t>(type), event);
        }

        std::size_t BindingCount() const { return m_Bindings.size(); }

    private:
        inline static std::uint32_t g_TypeIdNext = 0;

        template <typename T>
        static std::uint32_t GetTypeId()
        {
            static const std::uint32_t id = g_TypeIdNext++;
            return id;
        }

        std::vector<EventBinding> m_Bindings;
    };

    class EventQueue
    {
    public:
        void Push(XMovement *event) { m_Queue.push(event); }

        XMovement *Pop()
        {
            if (m_Queue.empty())
            {
                return nullptr;
            }
            XMovement *e = m_Queue.front();
            m_Queue.pop();
            return e;
        }

        bool Empty() const { return m_Queue.empty(); }
        std::size_t Size() const { return m_Queue.size(); }

    private:
        std::queue<XMovement *> m_Queue;
    };

    class Layer
    {
    public:
        virtual ~Layer() = default;

        virtual void OnAttach() {}
        virtual void OnDetach() {}
        virtual void OnUpdate() {}
        virtual void OnEvent(XMovement *event) { (void)event; }
    };

    class LayerStack
    {
    public:
        void PushLayer(Layer *layer)
        {
            if (!layer)
            {
                return;
            }
            m_Layers.push_back(layer);
            layer->OnAttach();
        }

        void PopLayer(Layer *layer)
        {
            auto it = std::find(m_Layers.begin(), m_Layers.end(), layer);
            if (it != m_Layers.end())
            {
                (*it)->OnDetach();
                m_Layers.erase(it);
            }
        }

        std::vector<Layer *> &GetLayers() { return m_Layers; }

    private:
        std::vector<Layer *> m_Layers;
    };

    // Application 持有的事件总线：队列 + 分层 + 分发器。
    class EventBus
    {
    public:
        EventDispatcher &Dispatcher() { return m_Dispatcher; }
        EventQueue &Queue() { return m_Queue; }
        LayerStack &Layers() { return m_Layers; }

        void Push(XMovement *event) { m_Queue.Push(event); }

    private:
        EventDispatcher m_Dispatcher;
        EventQueue m_Queue;
        LayerStack m_Layers;
    };
}
