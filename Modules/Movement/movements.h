#pragma once
#include "Core/Core.h"
#include <functional>
#include <vector>
#include <sstream>
#include <queue>
#include <unordered_map>
#include "Log/XYLog.h"
namespace X_Y
{
	// 窗口发送消息->winproc转化成某种行为，放入消息队列--
	//  ->分发器从队列中取出某种行为，调用所有监听该行为的函数
	// 消息队列和分发器都是全局，程序所持有

	// 添加动作（程序行为,窗口行为，鼠标键盘行为），或者说系统事件
	// 类型固定
	// 设计希望窗口具有某些行为，这些行为将触发逻辑事件，
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

	using MovementSender = void *;	 // 发起者（窗口/任意对象）
	using MovementReceiver = void *; // 接收者（任意对象）
	struct XMovement
	{
		virtual ~XMovement() = default;
		XMovement(MovementSender s) : sender(s) {};
		virtual bool DispatchEvent(void *dispatcher) = 0;
		MovementSender sender = nullptr;
		bool Handled = false;
	};
	// 消息队列知道行为及其发起者
	// 分发器通过匹配消息队列的信息寻找监听者执行其回调
	// 所以分发器维护了全局监听表
	// 接下来写分发器
	// using MovementHandler = std::function<void(XMovement* event)>;
	using MovementHandler = std::function<void(const XMovement &)>;
	using EventType = uint32_t;
	using TypeID = uint32_t;
	struct MovementBinding
	{
		MovementSender sender;	   // 谁发的
		EventType type;			   // 什么行为
		TypeID typeId;			   // 什么类型
		MovementReceiver receiver; // 谁接收
		MovementHandler handler;   // 怎么处理
	};
	class MovementDispatcher
	{
	public:
		// -------------------------------------------------------------------------
		// 模板 Connect：自动识别枚举类型，存入同一个列表
		// -------------------------------------------------------------------------
		template <typename EnumT>
		void Connect(
			MovementSender sender,
			EnumT type,
			MovementReceiver receiver,
			MovementHandler handler)
		{
			XY_CORE_ASSERT(std::is_enum_v<EnumT>, "必须是枚举类型");
			XDEBUG("入队加一:{}", type)
			MovementBinding bind{};
			bind.sender = sender;
			bind.type = static_cast<EventType>(type);
			bind.typeId = GetTypeId<EnumT>(); // 自动区分枚举类型
			bind.receiver = receiver;
			bind.handler = std::move(handler);

			m_Bindings.push_back(std::move(bind)); // 全部存在一起
		}

		// -------------------------------------------------------------------------
		// 模板 Disconnect：按【枚举+类型+接收者】删除
		// -------------------------------------------------------------------------
		template <typename EnumT>
		void disConnect(
			MovementSender sender,
			EnumT type,
			MovementReceiver receiver)
		{
			XY_CORE_ASSERT(std::is_enum_v<EnumT>, "必须是枚举类型");

			TypeID typeId = GetTypeId<EnumT>();
			EventType typeVal = static_cast<EventType>(type);

			for (auto it = m_Bindings.begin(); it != m_Bindings.end();)
			{
				if (it->sender == sender &&
					it->typeId == typeId &&
					it->type == typeVal &&
					it->receiver == receiver)
				{
					it = m_Bindings.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		// -------------------------------------------------------------------------
		// 全局 Disconnect：按【sender + receiver】删除（你原来的）
		// 精确匹配：只删 sender 和 receiver 都等于给定值的那些绑定。
		// -------------------------------------------------------------------------
		void disConnect(
			MovementSender sender,
			MovementReceiver receiver)
		{
			for (auto it = m_Bindings.begin(); it != m_Bindings.end();)
			{
				if (it->receiver == receiver && it->sender == sender)
				{
					XDEBUG("回调被删除");
					it = m_Bindings.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		// -------------------------------------------------------------------------
		// 全局 Disconnect：按【单个对象】删除 —— 删掉与该对象相关的【所有】订阅。
		//
		// ⚠️ 语义（2026-09-14 砚台拍板改定）：
		//   传进来的 ptr 只要出现在某条绑定的 sender 或 receiver 上任一位置，
		//   这条绑定就被删。也就是"把我和事件系统的所有关系一次清干净"。
		//
		//   改前只比 receiver —— 于是 `connect(this, ..., this, ...)` 这种
		//   this 兼作两者的绑定能删掉，但 `sender == this && receiver != this`
		//   那类（本对象发事件给别人处理）会漏网。
		//   对象要销毁时漏网 = dispatcher 里残留指向野指针的绑定 = UAF。
		//
		//   现在统一成"删我相关的全部"，调用方只需 `disConnect(this)` 一次，
		//   不必再记得补一次 disConnect(self, self)。
		// -------------------------------------------------------------------------
		void disConnect(MovementReceiver receiver)
		{
			for (auto it = m_Bindings.begin(); it != m_Bindings.end();)
			{
				if (it->receiver == receiver || it->sender == receiver)
				{
					XDEBUG("回调被删除");
					it = m_Bindings.erase(it);
				}
				else
				{
					++it;
				}
			}
		}
		// -------------------------------------------------------------------------
		// ✅ Dispatch：接收事件，自动匹配【枚举类型+值】
		//
		// ⚠️⚠️ 必须先快照再执行 —— 这是本函数唯一容易踩死的地方：
		//   回调（handler）里很可能调 disConnect / Connect（例如
		//   XWidget::destroy() 里就有 disConnect(this)，而它正是被
		//   WindowClose 的 handler 调起来的）。若直接 `for (auto& bind : m_Bindings)`
		//   边遍历边让回调改 m_Bindings：
		//     · erase 让后续元素前移 → 范围 for 的迭代器失效
		//     · bind 是引用，指向的元素可能已被搬移/销毁
		//   实测会抛 std::bad_function_call / 崩（同一 sender 挂多个 handler 时必现）。
		//
		//   修法：派发前把【命中的 handler】拷贝进本地快照，只遍历快照。
		//   于是回调里对 m_Bindings 的任何增删都只作用于真实表，不影响本轮遍历。
		//
		// 语义（有意如此）：快照 = 派发开始那一刻在场的监听者。
		//   回调期间新注册的 handler 【不会】收到本次事件（事件发给当时在场的人）；
		//   回调期间被摘掉的 handler，若在快照里则【仍然】会被调用一次
		//   （它拷贝出来时还是有效订阅，语义上属于"本轮已受理"）。
		// -------------------------------------------------------------------------
		bool DispatchEvent(uint32_t typeId, uint32_t typeVal, XMovement *event)
		{
			if (!event)
				return false;

			// ① 快照：收集本轮命中的 handler（拷贝，与 m_Bindings 脱钩）
			std::vector<MovementHandler> pending;
			for (const auto &bind : m_Bindings)
			{
				if (bind.sender == event->sender &&
					bind.typeId == typeId && // 严格匹配枚举类型
					bind.type == typeVal)	 // 严格匹配枚举值
				{
					pending.push_back(bind.handler);
				}
			}

			// ② 执行快照：此刻 m_Bindings 怎么变都无所谓了
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
		template <typename EMT>
		bool Dispatch(EMT type, XMovement *event)
		{
			uint32_t typeId = GetTypeId<EMT>();
			uint32_t typeVal = static_cast<uint32_t>(type);
			// if (DispatchEvent(typeId, typeVal, event))
			//{
			//	return true;
			// }
			// else
			//{
			//	return false;
			// }
			return DispatchEvent(typeId, typeVal, event);
		}

	private:
		// -------------------------------------------------------------------------
		// 每个枚举类型生成唯一ID
		// -------------------------------------------------------------------------
		inline static uint32_t g_TypeIdNext = 0;
		template <typename T>
		static uint32_t GetTypeId()
		{
			static const uint32_t id = g_TypeIdNext++;
			// XINFO("类型id{}",id)
			return id;
		}

	private:
		// 所有绑定存在同一个列表！
		std::vector<MovementBinding> m_Bindings;
	};

	// 全局消息队列
	class MovementQueue
	{
	public:
		// 窗口/发起者 把行为推入队列
		void Push(XMovement *event)
		{
			m_Queue.push(event);
		}
		// 取出一个行为
		XMovement *Pop()
		{
			if (m_Queue.empty())
				return nullptr;
			XMovement *e = m_Queue.front();
			m_Queue.pop();
			return e;
		}
		bool Empty() const { return m_Queue.empty(); }

	private:
		std::queue<XMovement *> m_Queue;
	};

	class Layer
	{
	public:
		virtual ~Layer() = default;

		// 层被挂载时调用
		virtual void OnAttach() {}

		// 层被卸载时调用
		virtual void OnDetach() {}

		// 每一帧更新
		virtual void OnUpdate() {}

		// 层接收事件
		virtual void OnEvent(XMovement *event) {}
	};

	class LayerStack
	{
	public:
		void PushLayer(Layer *layer)
		{
			m_Layers.push_back(layer);
			layer->OnAttach();
		}

		void PopLayer(Layer *layer)
		{
			auto it = std::find(m_Layers.begin(), m_Layers.end(), layer);
			if (it != m_Layers.end())
			{
				layer->OnDetach();
				m_Layers.erase(it);
			}
		}

		std::vector<Layer *> &GetLayers() { return m_Layers; }

	private:
		std::vector<Layer *> m_Layers;
	};
	// 窗口的行为类型，携带自己的消息
	// 窗口只负责将行为发送给消息队列
	class Movement : public XMovement
	{
	public:
		virtual ~Movement() = default;
		Movement(MovementSender s) : XMovement(s) {};
		virtual MovementType GetType() const = 0;
		virtual const char *GetName() const = 0;
		virtual int GetCategoryFlags() const = 0;
		virtual std::string toString() const
		{
			return GetName();
		};
		bool DispatchEvent(void *dispatcher) override
		{
			auto *d = static_cast<MovementDispatcher *>(dispatcher);
			bool flag = d->Dispatch(this->GetType(), this);
			if (this->GetType() == MovementType::WindowPaint)
			{
				return flag;
			}
			// if (flag)
			//{
			//	XDEBUG("{}事件处理成功{}", reinterpret_cast<uintptr_t>(this->sender),this->GetName())
			// }
			// else
			//{
			//	XDEBUG("{}事件处理失败{}", reinterpret_cast<uintptr_t>(this->sender),this->GetName())
			// }
			return flag;
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
	using EventSender = MovementSender;		// 发起者（窗口/任意对象）
	using EventReceiver = MovementReceiver; // 接收者（任意对象）
	// 用于逻辑事件
	// 可以继续仿写队列和分发器
	class Event : public XMovement
	{
	public:
		bool Handled = false;
		EventSender sender = nullptr;
		virtual ~Event() = default;
		Event(EventSender s) : XMovement(s) {};
		virtual int GetCategoryFlags() const = 0;

		virtual const char *GetName() const = 0;
		virtual std::string toString() const
		{
			return GetName();
		};
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
