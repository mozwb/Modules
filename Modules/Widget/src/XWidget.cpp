#include "Widget/XWidget.h"
#include "Log/XYLog.h"
#include <cstdint> // uintptr_t（日志里打印对象地址用）

namespace X_Y
{

    XWidget::XWidget(XWidget *parent, StorageTag storage)
        : m_parent(parent), m_Storage(storage)
    {
        XDEBUG("XWidget 构造执行,parent={},storage={}",
               (uintptr_t)parent, storage == StorageTag::Heap ? "Heap" : "Stack")
        auto app = Application::instance();
        if (!app)
        {
            XY_CORE_ASSERT(false, "Application instance is null!");
            return;
        }

        if (m_parent)
        {
            XDEBUG("子窗口构造执行")
            connect(this, MovementType::WindowClose, this, &XWidget::destroy);
            connect(parent, MovementType::WindowClose, this, &XWidget::destroy);
        }
        else
        {
            // 顶层窗口：关它就关它自己。
            // ⚠️ "关哪个窗口退出程序"不再由"谁先建"决定 —— 改由
            //    Application::SetMainWindow 显式指定（见 Application.h）。
            connect(this, MovementType::WindowClose, this, &XWidget::destroy);
        }

        Connect(this, MovementType::WindowResize, this, [this](const XMovement &e)
                {
        auto& resize = dynamic_cast<const WindowResize&>(e);
        XDEBUG("窗口resize: {}x{}", resize.GetWidth(), resize.GetHeight()); });

        // ── 原生销毁 → 对象自毁（跨平台语义，不写在 Win32 里）──
        // 平台消息泵在 WM_DESTROY 时发 WindowDestroy（见 Win32WndProc.cpp），
        // 这里接住它，把"这个 C++ 对象该退场了"这件事交给 Application 的
        // 延迟回收安全点 —— 不能当场 delete，理由见 OnNativeDestroyed 注释。
        connect(this, MovementType::WindowDestroy, this, &XWidget::OnNativeDestroyed);
    }

    // ── 析构：兜底退订 ──
    // 覆盖"从没收到过 WindowDestroy 就死了"的路径（如从没 show() 过的窗口被 delete）
    // —— 那种情况下 OnNativeDestroyed 永远不会跑，订阅只能靠这里兜。
    // 走正规路径时 OnNativeDestroyed 已先退过一次，这里再退是幂等的（表里没了就 no-op）。
    //
    // 单参 disConnect(ptr) 现在的语义是「删掉与该对象相关的所有订阅」
    // （sender 或 receiver 任一命中即删），一次调用就够 —— 见 movements.h 注释。
    XWidget::~XWidget()
    {
        disConnect(this);
    }

    // ── 窗口原生销毁 → 登记延迟回收 ──
    //
    // 调用时机：WM_DESTROY 之后，dispatcher 正在遍历绑定表把 WindowDestroy
    // 送过来。此刻 ProcessEvents / DispatchEvent 全都还在栈上且持有 this，
    // 直接 delete this 会立刻 迭代器失效 + UAF。
    //
    // ⇒ 只入队，等 ProcessEvents 一轮结束后的安全点由 Application 真正释放。
    void XWidget::OnNativeDestroyed()
    {
        // 门闩：已经处理过就不再处理（防重复入队 → flush 时 delete 两遍）
        if (m_RecycleQueued)
        {
            XDEBUG("[窗口回收] {} @{} 重复收到 WindowDestroy，门闩拦下",
                   toString(), (uintptr_t)this)
            return;
        }
        m_RecycleQueued = true;

        // 从 Application 的托管名单摘除：本对象即将退场，
        // 不该再被退出清理（Application::Shutdown）当成"活着"去 destroy。
        if (Application *app = Application::instance())
            app->ForgetOwnedWindow(this);

        // ════════════════════════════════════════════════════════
        // 栈对象：只退订，【绝不 delete】
        //   对象内存归 C++ 作用域管（栈/成员）。这里若 delete 就是打在栈上，必崩。
        //   窗口 HWND 已经没了，但 C++ 对象要等作用域结束才析构 —— 这是使用方
        //   自己选 Stack 时就接受的语义。
        // ════════════════════════════════════════════════════════
        if (!IsHeapAllocated())
        {
            XDEBUG("[窗口回收] {} @{} 是【栈对象】→ 只退订，不 delete（内存归作用域）",
                   toString(), (uintptr_t)this)
            disConnect(this);
            return;
        }

        XDEBUG("[窗口回收] {} @{} 是【堆对象】收到 WindowDestroy → 登记延迟回收",
               toString(), (uintptr_t)this)

        Application *app = Application::instance();
        if (!app)
        {
            // 没有 Application（异常场景，如已析构）→ 退回直接删除。
            // 此时不在 dispatcher 遍历中，相对安全。
            XWARN("[窗口回收] {} @{} 无 Application，退回直接 delete", toString(), (uintptr_t)this)
            delete this;
            return;
        }

        // ── 死前退订 ──
        // 目的：对象马上要被 delete，dispatcher 里不能残留任何指向它的绑定，
        //       否则后续派发会打到野指针。
        //
        // 单参 disConnect(ptr) 的语义（2026-09-14 砚台改定）：
        //   「删掉与该对象相关的所有订阅 —— sender 或 receiver 任一命中即删」。
        //   所以一句 `disConnect(this)` 就把本窗口与事件系统的所有关系清干净，
        //   不需要再补 disConnect(self, self)（旧语义只比 receiver 时才需要）。
        disConnect(this);

        app->DeferRecycle([this]()
                          {
                              XDEBUG("[窗口回收] @{} 延迟回收执行 → delete this", (uintptr_t)this)
                              delete this;
                          });
        XDEBUG("[窗口回收] {} @{} 已入队（将在本轮 ProcessEvents 结束时释放）",
               toString(), (uintptr_t)this)
    }

    bool XWidget::show(ShowCmd nShow)
    {
        if (BaseWin::Show(nShow))
            return true;
        XINFO("窗口未创建，自动创建窗口: {}", m_title);
        if (this->Create(m_title, GetActualWidth(), GetActualHeight(),
                         m_WindowStyle, m_ParentHwnd))
        {
            return this->show(nShow);
        }
        XERROR("窗口创建失败")
        return false;
    }

    void XWidget::setTitle(const char *title)
    {
        m_title = title;
        this->SetTitle(title);
    }

    void XWidget::setSize(uint width, uint height)
    {
        SetActualSize(width, height);
    }

    void XWidget::destroy()
    {
        // ⚠️⚠️ 这里【不能】调 disConnect(this)！
        //
        // 单参 disConnect(ptr) 的语义是「删掉与该对象相关的所有订阅」
        // （sender 或 receiver 任一命中即删）。而构造函数里注册了：
        //   connect(this, WindowDestroy, this, &XWidget::OnNativeDestroyed)
        // 它的 sender 和 receiver 都是 this —— 所以 disConnect(this)
        // 必然把它【一起删掉】。于是下面 DestroyWindow 发出的 WindowDestroy
        // 事件在派发时找不到监听者 → OnNativeDestroyed 永远不被调用 →
        // 对象永远不会被回收（泄漏）。
        //
        // 正确时机：退订应该发生在对象【真要死】的时候，也就是
        // OnNativeDestroyed() 里（析构里还有一层兜底）——
        // destroy() 只是「请求销毁窗口」，对象此刻还活着，订阅不该拆。
        //
        // 实测记录：改前 destroy() 路径下 WindowDestroy 绑定被删光，
        // 自毁回调收不到、对象泄漏（见 DEVLOG 2026-09-14）。
        //
        // ⚠️ 老代码那个 disConnect 的另一个作用本是「防止重复关闭」。
        //    Destroy() 内部本来就有 `if (m_Hwnd)` 保护，重复调用是 no-op，
        //    所以去掉退订不影响防重入。
        this->Destroy();
    }

    void XWidget::disconnectPa()
    {
        if (m_parent)
        {
            disConnect(m_parent, this);
        }
    }

    void XWidget::releaseSelf()
    {
        disconnectPa();
        m_parent = nullptr;
    }

}
