# WINDOW项目

# 目录
- [WINDOW项目](#window项目)
- [目录](#目录)
  - [架构](#架构)
  - [Application](#application)
  - [XWidget](#xwidget)
  - [Movements](#movements)
    - [消息](#消息)
    - [消息队列](#消息队列)
    - [消息处理的机制](#消息处理的机制)
  - [消息处理的使用](#消息处理的使用)
  - [补充](#补充)
---
## 架构

- 大致可以看作三个部分

  1. Application    负责程序的循环，感觉就像房子的布局
  2. XWidget        提供简单窗口，像是房子的装修
  3. Movement       包括事件，消息队列，事件分发器，像是房子的电线
   
---  
## Application
- 先简单看下application,感觉这个就是负责app的全局的东西，可能要因app而异吧，后面在贴全部代码好了。
```cpp

#这些都是可以重写的虚函数
    #消息循环，可以重写这个，也可以直接在主函数里写while循环
    void Application::exec()
    {
        while (Running)
        {
            pushEvents();
            ProcessEvents();
        }
    }

    #这个派发win32的原生消息，目前所有的代码都只支持windows操作系统
    void Application::pushEvents() {
        MSG msg{};
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    #自己的逻辑事件
    void Application::pushEvents(XMovement* e) {
        m_eventQueue.Push(e);
    }

    #如何处理消息
    void Application::ProcessEvents() {
        while (!m_eventQueue.Empty()) {
            X_Y::XMovement* baseEvt = m_eventQueue.Pop();
            if (!baseEvt) continue;
            baseEvt->DispatchEvent(static_cast<void*>(&m_dispatcher));
            delete baseEvt;
        }
    }

```

---

## XWidget

- 这个大致分成了三部分来写（因为要考虑到不同操作系统的实现）

1. BaseWin 操作系统的简单封装
2. XWidget 基于baseWin而实现的统一上层的窗口，在这层希望开始看不到操作系统的东西
3. WinCore 某个平台的具体实现，（windows的窗口需要提前注册窗口类InitGlobalWindowClass，然后还要设置好LRESULT CALLBACK StaticWndProc回调）

- winConfigure就是打开一下控制台，可以不考虑
<div style="display:flex;align-items:center;text-align:center;width:100%;margin:16px 0;">
<span style="flex:1;border-bottom:4px dashed #000;"></span>
<h1 id="Widget" style="margin:0 12px;white-space:nowrap;">XWidget</h1>
<span style="flex:1;border-bottom:4px dashed #000;"></span>
</div>

- 下面是XWidget，单从头文件来看的话其实已经没有操作系统的细节了，然后它干的活也不多，也就是显示，关闭之类的简单操作就没了
- 虽然我还加了什么上下文以及render相关的方法，但是感觉这些可能是特定的窗口才需要的（其实这里我不是很清楚是不是所有窗口都需要这些，就先加上了）
    ```cpp
		using Base = BaseWin;
		using MovementType = X_Y::MovementType;
	
		class XWidget :protected Base {
		public :
			explicit XWidget(XWidget* parent=nullptr);
			~XWidget(){destroy();}

			//show中执行创建，才有窗口指针
			//除非主动调用create，否则应该show完之后在执行其他步骤
			bool show(showtype nShow=SHOW);
			void close();
			void destroy();
			//切断与父类相关的所有回调
			void disconnectPa();
			//让自己变成独立窗口
			void releaseSelf();

			void  Render() {
				XY_CORE_ASSERT(m_Context, "上下文不能为空");
				if (!m_Context->IsCurrent())
					if(!m_Context->MakeCurrent())
						XY_CORE_ASSERT(false, "MakeCurrent失败");
				this->onRender();
				m_Context->SwapBuffers();
			}

			virtual void onRender() {};
			std::string getname() { return toString();}
			std::string toString()const override{
				return m_title;
			}
			GraphicsContext* get_context() {
				return m_Context.get();
			}
			uint get_width() {
				return m_width;
			}
			uint get_height() {
				return m_height;
			}
			void setTitle(const char* title);
			void setSize(uint width, uint height);
			XWidget* getParent() const { return m_parent; }

			void SwapBuffers() {
				m_Context->SwapBuffers();
			}



			// C++11及以后：直接删除拷贝构造、拷贝赋值
			XWidget(const XWidget&) = delete;
			XWidget& operator=(const XWidget&) = delete;

			// 要不要移动看需求，也可以一起禁掉
			//XWidget(XWidget&&) = delete;
			//XWidget& operator=(XWidget&&) = delete;
			template<typename T = GraphicsContext,class... Args>
			void setGrContext(Args&& ... args) {
				XY_CORE_ASSERT((std::is_base_of_v<GraphicsContext,T>),"T must inherit from GraphicsContext")
				m_Context = CreateScope<T>(std::forward<Args>(args)...);
			}
			template<typename T = GraphicsContext>
			void createContext() {
				setGrContext<T>(this->GetNativeWindow());
				m_Context->Init();
			}
			bool create() {return this->Create(m_title, m_width, m_height);}
		private:
			XWidget* m_parent = nullptr;
			Scope<GraphicsContext> m_Context;
			const char* m_title = "X_Y";
			uint m_width = 800;
			uint m_height = 600;
		};
    ```

- 这里是实现的文件,原本是有操作系统的细节的但是我把他们下移了
```cpp
namespace X_Y {
   XWidget::XWidget(XWidget* parent):
            m_parent(parent)
        {
            XDEBUG("XWidget 构造执行，parent={}", (uint16_t)parent)
            auto app = Application::instance();
            if (!app) {
                // 处理单例为空的异常，比如直接assert或日志
                XY_CORE_ASSERT(false,"Application instance is null!");
                return;
            }

            if (m_parent) {
                // 子窗口：绑定自身销毁和父窗口关闭时销毁
                XDEBUG("子窗口构造执行")
                connect(this, MovementType::WindowClose, this, &XWidget::destroy);
                connect(parent, MovementType::WindowClose, this, &XWidget::destroy);
            }
            else if (!app->IsFirstWin()) {
                // 顶级窗口：更新主窗口标记，并绑定到应用退出
                app->updateFirstWin();
                connect(this, MovementType::WindowClose, app, &Application::appClose);
            }
            else {
                //主窗口逻辑
                connect(this, MovementType::WindowClose, this, &XWidget::destroy);
            }
        }
        bool XWidget::show(showtype nShow)
        {   

            if (this->Show(nShow)) return true;
            // 2. 没有句柄 → 只创建一次！！！
            XINFO("窗口未创建，自动创建窗口: {}", m_title);
            if (this->Create(m_title, m_width, m_height)) {
                return this->show(nShow);
            }
            XERROR("窗口创建失败")
            return false;
        }
        void XWidget::setTitle(const char* title) {
            m_title = title;
            this->SetTitle(title);
        }

        void XWidget::setSize(uint width, uint height) {
            m_width = width;
            m_height = height;
        }
        void XWidget::close() {
            XINFO("调用colse函数")
                this->Close();
        }
        void XWidget::destroy() {
            disConnect(this);
            this->Destroy();
        }
        void XWidget::disconnectPa() {
            if (m_parent) {
                disConnect(m_parent, this);
            }
        }
        void XWidget::releaseSelf() {
            disconnectPa();
            m_parent=nullptr;
        }
    }
```
<div style="display:flex;align-items:center;text-align:center;width:100%;margin:16px 0;">
<span style="flex:1;border-bottom:4px dashed #000;"></span>
<h1 id="BaseWin" style="margin:0 12px;white-space:nowrap;">BaseWin</h1>
<span style="flex:1;border-bottom:4px dashed #000;"></span>
</div>

算是XWidget的底层了，负责干和操作系统交互的脏活
```cpp
#pragma once
#include<string>
#ifdef XY_PLATFORM_WINDOWS
#include<windows.h>
#define NativeWindow  HWND
#elif 
    ERROR("不支持其他os")
#endif

#define NHWD NativeWindow
namespace X_Y {
        typedef unsigned int uint;
        enum showtype {
            HIDE,
            NORMAL,
            MINIMIZED,
            MAXIMIZED,
            NOACTIVATE,
            SHOW,
            MINIMIZE,
            MINNOACTIVE,
            SHOWNA,
            RESTORE,
            DEFAULT
        };
    class BaseWin
    {
    public:
        BaseWin() = default;
        void SetNHWD(NHWD& hwnd) { m_Nhwd = hwnd; }
        NHWD GetNHWD()const { return m_Nhwd; }
        NHWD GetNativeWindow()const { return GetNHWD();}
    protected:
        bool Show(showtype nshow=SHOW);
        void Close();
        void SetTitle(const char* title);
        void Destroy();
        virtual bool Create(const char* title, uint width, uint height);
        virtual std::string toString() const { return "BaseWindow"; }
    private:
        NHWD m_Nhwd = nullptr;
    };
}
```
下面是实现，可见只提供了windows平台的，如果想使用别的平台的，应该得用宏隔断来实现，其实感觉也可以考虑一下,Render模块的那种方式，但是至少还要再接一层，感觉好麻烦
```cpp
#include<Window/include/BaseWIn.h>
#include"Log/include/XYLog.h"
#include<Window/include/WinCore.h>
namespace X_Y {
#ifdef XY_PLATFORM_WINDOWS
	bool BaseWin::Show(showtype nshow) {
		if (m_Nhwd) {
			ShowWindow(m_Nhwd, nshow);
			UpdateWindow(m_Nhwd);
			return true;
		}
		return false;
	}

	void BaseWin::Close() {
		XINFO("调用colse函数")
		if (m_Nhwd)	this->Show(HIDE);
	}
	void BaseWin::Destroy() {
		if (m_Nhwd) {
			DestroyWindow(m_Nhwd);
			m_Nhwd = nullptr;
		}
	}
	void BaseWin::SetTitle(const char* title) {
		if(m_Nhwd)  SetWindowTextA(m_Nhwd, title);
	}
    bool BaseWin::Create(const char* title, uint width, uint height) {
        HINSTANCE hInstance = X_Y::WinCore::g_hInstance;

        // 1. 把 char* 变量安全转为宽字符串
        wchar_t wTitle[256] = { 0 };

        // 🔥 这里必须用 CP_ACP（Windows 本地编码）
        MultiByteToWideChar(CP_ACP, 0, title, -1, wTitle, _countof(wTitle));

        HWND hwnd = CreateWindowEx(
            0,
            X_Y::WinCore::g_szClassName,
            wTitle,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            width, height,
            NULL, NULL,
            hInstance,
            this
        );
        if (!hwnd) {
            DWORD err = GetLastError();
            XERROR("CreateWindowEx failed, error code: {}", err);
            return false;
        }
        XINFO("hwnd:{}", (uint64_t)hwnd)
            if (hwnd) {
                this->SetNHWD(hwnd);
                return true;
            }
        return false;
	}
#endif
}
```
<div style="display:flex;align-items:center;text-align:center;width:100%;margin:16px 0;">
<span style="flex:1;border-bottom:4px dashed #000;"></span>
<h1 id="WinCore" style="margin:0 12px;white-space:nowrap;">WinCore</h1>
<span style="flex:1;border-bottom:4px dashed #000;"></span>
</div>

WinCore负责的也很简单，因为windows需要注册窗口类，它只负责干这个
```cpp
#pragma once
#include"Log/include/XYLog.h"
#include"Application/include/Application.h"
#include"Widget/Movement/Movements.h"
#include"Widget/Movement/AppMovement.h"
#include"Widget/Movement/KeyMovement.h"
#include"Widget/Movement/MouseMovement.h"
#include"Input/include/MapCode.h"
#include"BaseWin.h"
#ifdef XY_PLATFORM_WINDOWS
#include<windows.h>
#include <tchar.h>
namespace X_Y {
    namespace WinCore {
        using BaseWin = X_Y::BaseWin;
        inline   WNDCLASSEX g_XYWindowClass = { 0 };
        inline  HINSTANCE g_hInstance = nullptr;
        inline  const TCHAR* g_szClassName = _T("X_YWindow");
        inline void SetHinstace(HINSTANCE& hInstance) { g_hInstance = hInstance;   }

        inline LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
        {
           BaseWin* pThis = nullptr;
            auto* app = Application::instance();
            // 1. 窗口创建时：绑定 C++ 对象与 HWND
            if (msg == WM_NCCREATE) {
                pThis = (BaseWin*)((CREATESTRUCT*)lParam)->lpCreateParams;
                SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
                pThis->SetNHWD(hwnd);
            }
            else {
                // 其他消息：从窗口附加数据取出对象
                pThis = (BaseWin*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
            }
            Movement* movement=nullptr;
            KeyCode key = NULL;
            MouseCode mbutoon = NULL;
            // 2. 如果对象存在，把 Win32 消息转换成 MovementType 事件并转发
            if (pThis)
            {
                switch (msg)
                {
                    // 窗口关闭
                case WM_CLOSE:
                {
                    movement = new WindowClose(pThis);
                    app->GetEventQueue().Push(movement);
                    return 0;
                }
                    // 窗口销毁
                case WM_DESTROY:
                {
                    movement = new WindowDestory(pThis);
                    app->GetEventQueue().Push(movement);
                    return 0;
                }
                    // 窗口大小改变
                case WM_SIZE:
                {
                    int width = LOWORD(wParam);
                    int height = HIWORD(wParam);
                    // 这里可以把宽高打包进事件数据，比如用一个 struct 或者直接传参数
                     movement = new WindowResize(pThis,width,height);
                    app->GetEventQueue().Push(movement);
                    return 0;
                }

                // 窗口移动
                case WM_MOVE:
                {
                     movement = new WindowMoved(pThis);
                    app->GetEventQueue().Push(movement);
                    return 0;
                }
                case WM_PAINT:
                {
                    movement = new WindowPaint(pThis);
                    app->GetEventQueue().Push(movement);
                    return 0;
                }
                // 窗口焦点变化（简单处理为焦点获得/失去，你可以再细分）
                case WM_SETFOCUS:
                {
                    movement = new WindowFouces(pThis);
                    app->GetEventQueue().Push(movement);
                    return 0;
                }

                    // 键盘按下
                case WM_KEYDOWN:
                case WM_SYSKEYDOWN:
                {
                    bool isRepeat = (lParam & (1 << 30)) != 0;
                    key = InputMapping::Translate(wParam);
                    movement = new KeyPressed(pThis, key, isRepeat);
                    app->GetEventQueue().Push(movement);
                    return 0;
                }
                    // 键盘抬起
                case WM_KEYUP:
                case WM_SYSKEYUP:
                {
                    key = InputMapping::Translate(wParam);
                    movement = new KeyReleased(pThis, key);
                    app->GetEventQueue().Push(movement);
                    return 0;
                }
                    // 鼠标移动
                case WM_MOUSEMOVE:
                {
                    float x = (short)LOWORD(lParam);
                    float y = (short)HIWORD(lParam);
                     movement = new MouseMoved(pThis,x,y);
                    app->GetEventQueue().Push(movement);
                    return 0;
                }

                // 鼠标左键按下
                case WM_LBUTTONDOWN:
                {
                    mbutoon = InputMapping::TranslateMouse(VK_LBUTTON);
                     movement = new MouseButtonPressed(pThis,mbutoon);
                    app->GetEventQueue().Push(movement);
                    return 0;
                }

                // 鼠标左键抬起
                case WM_LBUTTONUP:
                {
                     mbutoon = InputMapping::TranslateMouse(VK_LBUTTON);
                     movement = new MouseButtonReleased(pThis, mbutoon);
                    app->GetEventQueue().Push(movement);
                    return 0;
                }

                // 鼠标右键按下
                case WM_RBUTTONDOWN:
                {
                    mbutoon = InputMapping::TranslateMouse(VK_RBUTTON);
                     movement = new MouseButtonPressed(pThis, mbutoon);
                    app->GetEventQueue().Push(movement);
                    return 0;
                }

                // 鼠标右键抬起
                case WM_RBUTTONUP:
                {
                     mbutoon = InputMapping::TranslateMouse(VK_RBUTTON);
                     movement = new MouseButtonReleased(pThis, mbutoon);
                    app->GetEventQueue().Push(movement);
                    return 0;
                }

                // 鼠标滚轮(垂直滚轮)
                case WM_MOUSEWHEEL:
                {
                    // 1. 从 wParam 中取出滚轮的滚动量（单位是 WHEEL_DELTA）
                    int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                    // 2. 把 delta 转换成偏移量（通常除以 WHEEL_DELTA，即 ±120）
                    float yOffset = static_cast<float>(delta) / WHEEL_DELTA;
                    movement = new MouseScrolled(pThis,0.0,yOffset);
                    app->GetEventQueue().Push(movement);
                    return 0;
                }
                }
            }

            // 3. 未处理的消息 → 交给系统默认处理
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }
       inline void InitGlobalWindowClass(HINSTANCE& hInstance)
        {
            // 已经初始化过就不再初始化
            if (g_XYWindowClass.cbSize != 0)
                return;

            // 你写的完美配置
            g_XYWindowClass.cbSize = sizeof(WNDCLASSEX);
            g_XYWindowClass.hInstance = hInstance;
            g_XYWindowClass.lpszClassName = g_szClassName;
            g_XYWindowClass.lpfnWndProc = StaticWndProc;
            g_XYWindowClass.style = CS_HREDRAW | CS_VREDRAW;
            g_XYWindowClass.cbClsExtra = 0;
            g_XYWindowClass.cbWndExtra = 0;
            g_XYWindowClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
            g_XYWindowClass.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
            g_XYWindowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
            g_XYWindowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            g_XYWindowClass.lpszMenuName = NULL;
        }
        inline void RegisterWinClass(HINSTANCE& hInstance) {
            XINFO("hInstance:{}",hInstance)
            InitGlobalWindowClass(hInstance);
            if (!RegisterClassEx(&g_XYWindowClass))
            {
                MessageBox(NULL, _T("窗口类注册失败"), _T("错误"), MB_ICONERROR);
                exit(EXIT_FAILURE);
            }
        }
    }
}
#endif 
```
StaticWndProc回调里把所有的原生消息拦截下来，加到了消息队列里。

<div style="display:flex;align-items:center;text-align:center;width:100%;margin:16px 0;">
<span style="flex:1;border-bottom:4px dashed #000;"></span>
<h1 id="EntryPoint" style="margin:0 12px;white-space:nowrap;">EntryPoint</h1>
<span style="flex:1;border-bottom:4px dashed #000;"></span>
</div>

因为使用了窗口,入口点就变为了int WINAPI WinMain了,所以这里是为了使用main所做的努力
```cpp
#include"Log/include/XYLog.h"

#ifdef XY_PLATFORM_WINDOWS
	#include"WinCore.h"
	#include"winConfigure.h"
	extern int main(int argc, char* argv[]);
		int WINAPI WinMain(
			_In_ HINSTANCE hInstance,//实例句柄
			_In_opt_ HINSTANCE hPrevInstance,//没用
			_In_ LPSTR     lpCmdLine,//命令行参数字符串
			_In_ int       nCmdShow//显示窗口的方式
	) {

		X_Y::allowConsole();
		XINFO("程序运行到入口")
		X_Y::WinCore::RegisterWinClass(hInstance);
		X_Y::WinCore::SetHinstace(hInstance);
		return main(__argc, __argv);
		}
#else
		ERROR("仅支持windows系统")
#endif
```
## Movements

感觉这里是最麻烦的地方了，上面不是转接了所有的消息么，然后要在这里发送。

### 消息
首先对于消息的认知，我把消息分成了两种：
1. 一种算是win32的原生消息，其实就是按键盘，动鼠标，还有窗口，app的一些固有事件，因为我觉得这些都是用户的动作，所以就成为Movemnet了
2. 第二种就是跑在程序里面的事件，我叫做Event（感觉这里就是为拓展留条路）

两种类型的消息的起源
```cpp
    using MovementSender = void*; // 发起者（窗口/任意对象）
	using MovementReceiver = void*; // 接收者（任意对象）

	struct XMovement{
		virtual ~XMovement() = default;
		XMovement(MovementSender s):sender(s){};
		virtual bool DispatchEvent(void* dispatcher) = 0;
		MovementSender sender = nullptr;
		bool Handled = false;
	};
```
下面是两种消息，但其实完整可用的只有上面的，Event还没有完成特化吧算是
```cpp
	//窗口的行为类型，携带自己的消息
	//窗口只负责将行为发送给消息队列
	class Movement :public XMovement {
	public:
		virtual ~Movement() = default;
		Movement(MovementSender s) :XMovement(s) {};
		virtual MovementType GetType() const = 0;
		virtual const char* GetName() const = 0;
		virtual int GetCategoryFlags() const = 0;
		virtual std::string toString()const {
			return GetName();
		};
		bool DispatchEvent(void* dispatcher) override {
			auto* d = static_cast<MovementDispatcher*>(dispatcher);
			bool flag= d->Dispatch(this->GetType(), this);
			if (this->GetType() == MovementType::WindowPaint) { return flag; }
			if (flag) {
				XDEBUG("事件处理成功{}",this->GetName())
			}
			else {
				XDEBUG("事件处理失败{}", this->GetName())
			}
			return flag;
		}
		bool IsInCategory(MovementCategory category) {
			return GetCategoryFlags() & category;
		}
		friend std::ostream& operator<<(std::ostream& os, const Movement& e)
		{
			return os << e.toString();
		}
	};
	using EventSender = MovementSender; // 发起者（窗口/任意对象）
	using EventReceiver = MovementReceiver; // 接收者（任意对象）
	//用于逻辑事件
	//可以继续仿写队列和分发器
	class Event :public XMovement {
	public:
		bool Handled = false;
		EventSender sender = nullptr;
		virtual ~Event() = default;
		Event(EventSender s) :XMovement(s) {};
		virtual int GetCategoryFlags() const = 0;

		virtual const char* GetName() const = 0;
		virtual std::string toString()const {
			return GetName();
		};
		bool IsInCategory(MovementCategory category) {
			return GetCategoryFlags() & category;
		}
		friend std::ostream& operator<<(std::ostream& os, const Event& e)
		{
			return os << e.toString();
		}
	};
#define EVENT_DISPATHC_INIT \
	bool DispatchEvent(void* dispatcher) override {\
	auto* d = static_cast<MovementDispatcher*>(dispatcher);\
	bool flag = d->Dispatch(this->GetType(), this);\
	if (flag) {\
		XDEBUG("事件处理成功{}", this->GetName())\
	}\
	else {\
		XDEBUG("事件处理失败{}", this->GetName())\
	}\
	return flag;\
	}
}
```
其实 <span style="color:#2196f3;">movements.h</span>的开头有提供movement消息的类型
```cpp
	//添加动作（程序行为,窗口行为，鼠标键盘行为），或者说系统事件
	//类型固定
	//设计希望窗口具有某些行为，这些行为将触发逻辑事件，
	enum MovementCategory
	{
		None=0,
		MTApplication = BIT(0),
		MTInput = BIT(1),
		MTKeyboard = BIT(2),
		MTMouse = BIT(3),
		MTMouseButton=BIT(4),
		MTWindow=BIT(5)
	};
enum class MovementType {
			None = 0,
			AppTick, AppUpdate, AppRender,
			WindowClose, WindowResize, WindowFocus, WindowMoved, WindowDestory, WindowPaint,
			KeyPressed, KeyReleased, KeyTyped,
			MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseScrolled

		};
#define MOVEMENT_CLASS_TYPE(type) static MovementType GetStaticType() { return MovementType::type; }\
								virtual MovementType GetType() const override { return GetStaticType(); }\
								virtual const char* GetName() const override { return #type; }

#define  MOVEMENT_CLASS_CATEGORY(category) virtual int GetCategoryFlags() const override { return category; }
```
这个基本是仿照Hazel的引擎来做的，但是改着改着，又不完全一样了，对于type你基本只要写自己的enmu类型然后改写一下宏就能用，（处理在之后），对于那个category目前还没有实现它的可续写，想添加只能在原处添加，因为其实我目前还没咋用到这个，但是hazel写了我也就写了，这样可能对事件分流有帮助吧

### 消息队列

上面定义了消息，然后再写一个消息队列接受所有消息，最后再用分发器处理

```cpp
//全局消息队列
	class MovementQueue {
	public:
		// 窗口/发起者 把行为推入队列
		void Push(XMovement* event) {
			m_Queue.push(event);
		}
		// 取出一个行为
		XMovement* Pop() {
			if (m_Queue.empty()) return nullptr;
			XMovement* e = m_Queue.front();
			m_Queue.pop();
			return e;
		}
		bool Empty() const { return m_Queue.empty(); }
	private:
		std::queue<XMovement*> m_Queue;
	};
```

### 消息处理的机制

我大概写了两种处理机制吧，其实从上面的窗口就能看出我写了Connect这种类似于qt信号与槽的方式来设置回调，
通过发送者，消息类型，监听者，回调来区分不同的消息处理，另一种是hazel的layer吧，把所有的回调都写到层栈里，然后接受到消息的时候去里面进行匹配，这样子可以把消息进行分层管理比如UI层，render层。感觉也蛮不错，但是我一开始写的是Connect的这种，所以其实比较成熟的是这个，毕竟目前的回调也就区区几个，管理也没那么复杂。我写了基本的层栈，以后可能会用到吧。

因为层栈的比较少就先贴出这个的。

```cpp
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
		virtual void OnEvent(XMovement* event) {}
	};

	class LayerStack
	{
	public:
		void PushLayer(Layer* layer)
		{
			m_Layers.push_back(layer);
			layer->OnAttach();
		}

		void PopLayer(Layer* layer)
		{
			auto it = std::find(m_Layers.begin(), m_Layers.end(), layer);
			if (it != m_Layers.end())
			{
				layer->OnDetach();
				m_Layers.erase(it);
			}
		}

		std::vector<Layer*>& GetLayers() { return m_Layers; }

	private:
		std::vector<Layer*> m_Layers;
	};

```

接着是分发器
先维护了一个数据结构
```cpp
	using MovementHandler = std::function<void(const XMovement&)>;
	using EventType = uint32_t;
	using TypeID = uint32_t;
	struct MovementBinding
	{
		MovementSender     sender;     // 谁发的
		EventType type;           // 什么行为
		TypeID typeId;           // 什么类型
		MovementReceiver   receiver;   // 谁接收
		MovementHandler    handler;    // 怎么处理
	};
```
然后是DisPatcher,维护了一个vector管理所有回调，提供了绑定Connect，解绑DisConnect(这个类型蛮多的)
然后处理的实现，我在里面实现了对不同枚举类型的处理，其实就是用GetTypeId()给它分了个ID
```cpp
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
			MovementHandler handler
		) {
			XY_CORE_ASSERT(std::is_enum_v<EnumT>, "必须是枚举类型");
			XDEBUG("入队加一；{}",type)
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
			MovementReceiver receiver
		) {
			XY_CORE_ASSERT(std::is_enum_v<EnumT>, "必须是枚举类型");

			TypeID typeId = GetTypeId<EnumT>();
			EventType typeVal = static_cast<EventType>(type);

			for (auto it = m_Bindings.begin(); it != m_Bindings.end();) {
				if (it->sender == sender &&
					it->typeId == typeId &&
					it->type == typeVal &&
					it->receiver == receiver)
				{
					it = m_Bindings.erase(it);
				}
				else {
					++it;
				}
			}
		}

		// -------------------------------------------------------------------------
		// 全局 Disconnect：按【sender + receiver】删除（你原来的）
		// -------------------------------------------------------------------------
		void disConnect(
			MovementSender sender,
			MovementReceiver receiver)
		{
			for (auto it = m_Bindings.begin(); it != m_Bindings.end();) {
				if (it->receiver == receiver && it->sender == sender)
				{
					XDEBUG("回调被删除");
					it = m_Bindings.erase(it);
				}
				else {
					++it;
				}
			}
		}

		// -------------------------------------------------------------------------
		// 全局 Disconnect：只按【receiver】删除（你原来的）
		// -------------------------------------------------------------------------
		void disConnect(MovementReceiver receiver)
		{
			for (auto it = m_Bindings.begin(); it != m_Bindings.end();) {
				if (it->receiver == receiver)
				{
					XDEBUG("回调被删除");
					it = m_Bindings.erase(it);
				}
				else {
					++it;
				}
			}
		}
		// -------------------------------------------------------------------------
		// ✅ Dispatch：接收事件，自动匹配【枚举类型+值】
		// -------------------------------------------------------------------------
		bool DispatchEvent(uint32_t typeId ,uint32_t typeVal,XMovement* event) {
			if (!event) return false;
			bool handled = false;
			for (auto& bind : m_Bindings) {
				if (bind.sender == event->sender &&
					bind.typeId == typeId &&      // 严格匹配枚举类型
					bind.type == typeVal)          // 严格匹配枚举值
				{
					bind.handler(*event);
					handled = true;
				}
			}

			event->Handled = handled;
			return handled;
		}
		template<typename EMT>
		bool Dispatch(EMT type,XMovement* event){
			uint32_t typeId= GetTypeId<EMT>();
			uint32_t typeVal = static_cast<uint32_t>(type);
			if (DispatchEvent(typeId, typeVal, event)) {		
					return true;
			}
			else {
				return false;
			}
			//return DispatchEvent(typeId, typeVal, event);
		}
	private:
		// -------------------------------------------------------------------------
		// 每个枚举类型生成唯一ID
		// -------------------------------------------------------------------------
		inline static uint32_t g_TypeIdNext = 0;
		template <typename T>
		static uint32_t GetTypeId() {
			static const uint32_t id = g_TypeIdNext++;
			//XINFO("类型id{}",id)
			return id;
		}

	private:
		// 所有绑定存在同一个列表！
		std::vector<MovementBinding> m_Bindings;
	};

```
Movements模块其他地方就是对于movement事件类型的具体实现，鼠标、键盘用到了input模块，（其实就是定义了自己的键盘、鼠标的id吧，然后是实现了和windows操作系统的转换，windwos按键转为我自己定义的（其实是glfw的），然后我自己定义的转成windwos的）

## 消息处理的使用

Ok，接下我们看看它是如何工作的。

XWidget绑定事件,我们可以看到在构造里有好几个Connect,然后destory的时候会解绑所有回调
```cpp
XWidget::XWidget(XWidget* parent):
            m_parent(parent)
        {
            XDEBUG("XWidget 构造执行，parent={}", (uint16_t)parent)
            auto app = Application::instance();
            if (!app) {
                // 处理单例为空的异常，比如直接assert或日志
                XY_CORE_ASSERT(false,"Application instance is null!");
                return;
            }

            if (m_parent) {
                // 子窗口：绑定自身销毁和父窗口关闭时销毁
                XDEBUG("子窗口构造执行")
                connect(this, MovementType::WindowClose, this, &XWidget::destroy);
                connect(parent, MovementType::WindowClose, this, &XWidget::destroy);
            }
            else if (!app->IsFirstWin()) {
                // 顶级窗口：更新主窗口标记，并绑定到应用退出
                app->updateFirstWin();
                connect(this, MovementType::WindowClose, app, &Application::appClose);
            }
            else {
                //主窗口逻辑
                connect(this, MovementType::WindowClose, this, &XWidget::destroy);
            }
        }

        void XWidget::destroy() {
            disConnect(this);
            this->Destroy();
        }
...... 之前有给出全部内容
```
你可能好奇为啥可以直接使用connect,这些都写到Application里了,毕竟它可是指挥部啊！！！(app负责程序循环)
app负责持有消息队列，dispatcher,XWidget发送消息，消息队列接受并取出给dispatcher，在文件的末尾我把disconnect和connect都暴露出来了
```cpp
namespace X_Y {
#ifdef XY_PLATFORM_WINDOWS    
    //这个管理了程序运行周期，接受win32消息转化系统事件，可以仿照这个写渲染层或者其他层
    //总线接受所有事件，只负责系统事件，其他再下发执行
    class Application
    {
    private:
        static Application* s_instance;
        MovementDispatcher m_dispatcher;
        MovementQueue m_eventQueue;
        bool Running = true;
        bool FirstWin = false;
        //如果你需要打包一些逻辑，你可以写层栈，从而减轻事件分发器压力
        LayerStack m_LayerStack;  

    public:
        // 1.构造、析构放protected，允许子类继承构造，禁止外部new
        Application(int argc, char* argv[]);
        virtual ~Application();// 2.虚析构，多态析构必备
        // 消息循环
        virtual void exec();
        virtual void pushEvents();
        virtual void pushEvents(XMovement* e);
        virtual void ProcessEvents();
        bool isRunning() { return Running; }
        void appClose() {
            Running = false;
            FirstWin = false;
        }
        MovementDispatcher& GetDispatcher() { return m_dispatcher; }
        MovementQueue& GetEventQueue() { return m_eventQueue; }
        static Application* instance()
        {
            return s_instance;
        }
        bool IsFirstWin() {
            return FirstWin;
        }
        void updateFirstWin() {
            FirstWin = true;
        }
        void PushLayer(Layer* layer) {
            m_LayerStack.PushLayer(layer);
        }
        void PopLayer(Layer* layer) {
            m_LayerStack.PopLayer(layer);
                    }

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;
    };
    inline Application* Application::s_instance = nullptr;
 
    template <typename EnumT>
    void Connect(
        MovementSender sender,
        EnumT type,            // 直接保留原始枚举！
        MovementReceiver receiver,
        MovementHandler handler
    ) {
        auto& dispatcher = Application::instance()->GetDispatcher();
        // 直接把 EnumT 传给 dispatcher，类型 100% 保留！
        dispatcher.Connect<EnumT>(sender, type, receiver, std::move(handler));
    }
    template <typename EnumT>
    void disConnect(
        MovementSender sender,
        EnumT type,
        MovementReceiver receiver
    ) {
        auto& dispatcher = Application::instance()->GetDispatcher();
        dispatcher.disConnect<EnumT>(sender, type, receiver);
    }
    inline void disConnect(
        MovementSender sender, 
        MovementReceiver receiver
        ) {
        Application* app = Application::instance();
        MovementDispatcher& dispatcher = app->GetDispatcher();
        dispatcher.disConnect(sender,receiver);
    }
    inline void disConnect(
        MovementReceiver receiver) {
        Application* app = Application::instance();
        MovementDispatcher& dispatcher = app->GetDispatcher();
        dispatcher.disConnect(receiver);
    }
    template <typename S, typename E, typename R>
    void connect(
        S* sender,
        E type,
        R* receiver,
        void (R::* slotFunc)(const XMovement&)
    ) {
        MovementHandler handler = [=](const XMovement& e) {
            (receiver->*slotFunc)(e);
            };
        Connect(sender, type, receiver, std::move(handler));
    }
    template <typename S, typename E, typename R>
    void connect(
        S* sender,
        E type,
        R* receiver,
        void (R::* slotFunc)(XMovement*)
    ) {
        MovementHandler handler = [=](const XMovement& e) {
            (receiver->*slotFunc)(const_cast<XMovement*>(&e));
            };
        Connect(sender, type, receiver, std::move(handler));
    }
    template <typename S, typename E, typename R>
    void connect(
        S* sender,
        E type,
        R* receiver,
        void (R::* slotFunc)()
    ) {
        MovementHandler handler = [=](const XMovement&) {
            (receiver->*slotFunc)();
            };
        Connect(sender, type, receiver, std::move(handler));
    }
```
接下来将迎来我们的一大痛点，那就是不同消息类型可能承载不同的信号，我当然可以用XMovemen统一接受但是我解析使用的时候必须得知道它是啥类型的才行（dispatcher要求这一点，只有这样它才能匹配到正确的回调，同时我也没指望着XMovement拥有所有类型的方法，成员变量），我知道这样子很怪但是它在需要承载某些消息需要交付而不是简单通知的时候会很方便。（我想如此）

这是目前的处理
```cpp
    void Application::ProcessEvents() {
        while (!m_eventQueue.Empty()) {
            X_Y::XMovement* baseEvt = m_eventQueue.Pop();
            if (!baseEvt) continue;
            baseEvt->DispatchEvent(static_cast<void*>(&m_dispatcher));
            delete baseEvt;
        }
    }
```
它寄希望于事件本身能通知dispathcer自己是啥类型的
```cpp
struct XMovement{
		virtual ~XMovement() = default;
		XMovement(MovementSender s):sender(s){};
		virtual bool DispatchEvent(void* dispatcher) = 0; ↙️由此可见端倪
		MovementSender sender = nullptr;
		bool Handled = false;   ↙️突然发觉这里貌似也没那么有用了
	};
	bool DispatchEvent(void* dispatcher) override {
			auto* d = static_cast<MovementDispatcher*>(dispatcher);
			bool flag= d->Dispatch(this->GetType(), this);
			if (this->GetType() == MovementType::WindowPaint) { return flag; }↙️movement屏蔽了这个事件
			if (flag) {
				XDEBUG("事件处理成功{}",this->GetName())
			}
			else {
				XDEBUG("事件处理失败{}", this->GetName())
			}
			return flag;
		}
#define EVENT_DISPATHC_INIT \
	bool DispatchEvent(void* dispatcher) override {\
	auto* d = static_cast<MovementDispatcher*>(dispatcher);\
	bool flag = d->Dispatch(this->GetType(), this);\
	if (flag) {\
		XDEBUG("事件处理成功{}", this->GetName())\
	}\
	else {\
		XDEBUG("事件处理失败{}", this->GetName())\
	}\
	return flag;\
	}    ↙️依旧可以用宏快速拓展
```
可能会疑问这样不是事件自己处理自己吗，但是dispathcer必须要求类型，所以事件必须至少提供一个gettype，而我希望类型可以拓展，所以它的返回值是不统一的。虽然可以把事件改成模板类，写一个 using type =T，但是我觉得这样子的话，让某些特殊事件的处理可以多些手脚写在DispatchEvent，而且其实我觉得分发器处理的是少量快速事件吧，放在全局最顶层，之后可以用层栈来处理大部分固定事件。
<details>
<summary>💡  Reflection -001 | 过去的写法 （点击展开查看详情）</summary>

可见它只支持movement,如果你有自己的Event类型将会很鸡肋
```cpp
 void Application::ProcessEvents() {
     while (!m_eventQueue.Empty()) {
         Movement* e = m_eventQueue.Pop();
         if (e) {
             m_dispatcher.DispatchEvent(e);
             auto* sender = (BaseWin*)e->sender;
             std::string senderName;
             if (sender) {
                 senderName = sender->toString();
             }
             if (e->Handled) {
                 if(e->GetType()!=X_Y::MovementType::WindowPaint)
                 XINFO("发起者:{} 事件{}：处理成功", senderName, e);
                 delete e;
             } // 记得释放
             else {
                 XINFO("发起者:{} 事件{}：处理失败", senderName, e);
                 delete(e);
             }
         }
     }
 }
```
## 补充
    由于之前考虑用宏实现跨平台，但是经过考虑貌似不太好，所以使用了工厂模式，但是分层设计是差不多的，依旧是XWidget供使用，然后base层消除平台差异，已经没有core层了，用虚函数写了一个WindowImpl来实现对各个操作系统的转发，这样子的话其实base层就已经是封装完好的了，但是为了适配之前写的代码，同时为了避免暴露底层方便使用，所以感觉转发一下貌似会更整洁
</details>