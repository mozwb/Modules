#include "../Container/Container.h"
#include "../DockLayout/DockLayout.h"
#include "UI/dock/Dock.h"
#include "../UiCore/UIEvent.h"
#include "Widget/Movement/MouseMovement.h"
#include "Widget/Movement/KeyMovement.h"
#include "Widget/EventHelpers.h"

namespace X_Y
{

    // 取相对本窗口客户区的逻辑坐标（命中路由统一用它）
    void GetClientPos(Container &self, int &x, int &y)
    {
        self.GetMouseScreenPos(x, y);
        self.ScreenToClient(x, y);
    }

    Container::Container(XWidget *parent, StorageTag storage)
        : XWidget(parent, storage)
    {
        // ⚠️ 坐标契约（改鼠标事件前必读）：
        //   所有鼠标 Movement（Pressed/Moved/Released/Scrolled）的 GetX/GetY
        //   必须携带【物理客户区坐标】。壳在这里统一做唯一一次
        //   ClientPhysicalToLogical，之后全链路都是逻辑坐标。
        //   → 若某个 Movement 的产生处提前转成了逻辑坐标（例如滚轮曾误用
        //     ScreenToClient 而不是 ScreenToClientPhysical），这里会【重复缩放】，
        //     150% DPI 下坐标被多除一次 1.5，表现为命中位置偏左上
        //     （"鼠标在底部/右侧 Dock 上滚轮没反应"就是这么来的）。
        //   产出侧契约见 Widget/src/Win32/Win32WndProc.cpp 的鼠标分支。

        // 文件拖拽是窗口行为；Container 负责开启，具体面板决定如何处理。
        EnableFileDrop(true);

        // 布局 resize → 同步 DockLayout 尺寸并重排
        Connect(this, MovementType::WindowResize, this,
                [this](const XMovement &)
                { OnWindowResize(); });

        // 鼠标按下 → UIEvent(Press) → 布局路由（可能拖分割线/喂激活面板）
        Connect(this, MovementType::MouseButtonPressed, this,
                [this](const XMovement &e)
                {
            const auto &mb = dynamic_cast<const MouseButtonPressed &>(e);
            UIMouseEvent uie;
            uie.action = MouseAction::Press;
            uie.button = static_cast<Input::MouseCode>(mb.GetMouseButton());
            uie.x = static_cast<int>(mb.GetX());
            uie.y = static_cast<int>(mb.GetY());
            ClientPhysicalToLogical(uie.x, uie.y);
            if (m_Layout)
            {
                m_Layout->RouteInput(uie);
                CaptureMouse();
            } });

        // 鼠标移动 → UIEvent(Move)
        Connect(this, MovementType::MouseMoved, this,
                [this](const XMovement &e)
                {
            const auto &mm = dynamic_cast<const MouseMoved &>(e);
            UIMouseEvent uie;
            uie.action = MouseAction::Move;
            uie.x = static_cast<int>(mm.GetX());
            uie.y = static_cast<int>(mm.GetY());
            ClientPhysicalToLogical(uie.x, uie.y);
            if (m_Layout)
                m_Layout->RouteInput(uie); });

        // 鼠标抬起 → UIEvent(Release)
        Connect(this, MovementType::MouseButtonReleased, this,
                [this](const XMovement &e)
                {
            const auto &mb = dynamic_cast<const MouseButtonReleased &>(e);
            UIMouseEvent uie;
            uie.action = MouseAction::Release;
            uie.x = static_cast<int>(mb.GetX());
            uie.y = static_cast<int>(mb.GetY());
            ClientPhysicalToLogical(uie.x, uie.y);
            if (m_Layout)
                m_Layout->RouteInput(uie);
            ReleaseMouseCapture(); });

        // 滚轮 → UIEvent(Scroll)
        Connect(this, MovementType::MouseScrolled, this,
                [this](const XMovement &e)
                {
            const auto &ms = dynamic_cast<const MouseScrolled &>(e);
            UIMouseEvent uie;
            uie.action = MouseAction::Scroll;
            uie.scrollDelta = (int)ms.GetYOffset();
            uie.x = static_cast<int>(ms.GetX());
            uie.y = static_cast<int>(ms.GetY());
            ClientPhysicalToLogical(uie.x, uie.y);
            if (m_Layout)
                m_Layout->RouteInput(uie); });

        // 按键 → UIKeyEvent(not char)
        Connect(this, MovementType::KeyPressed, this,
                [this](const XMovement &e)
                {
            const auto &kp = dynamic_cast<const KeyPressed &>(e);
            UIKeyEvent uie;
            uie.key = kp.GetKeyCode();
            uie.isChar = false;
            if (m_Layout)
                m_Layout->RouteInput(uie); });

        // 字符 → UIKeyEvent(char)
        Connect(this, MovementType::KeyTyped, this,
                [this](const XMovement &e)
                {
            const auto &kt = dynamic_cast<const KeyTyped &>(e);
            UIKeyEvent uie;
            uie.ch = (wchar_t)kt.GetKeyCode();
            uie.isChar = true;
            if (m_Layout)
                m_Layout->RouteInput(uie); });
    }

    Container::~Container()
    {
        // ⚠️ 顺序：
        //  1. 先断开事件连接（免得析构期间还有回调进来）
        //  2. 让布局与壳脱钩（清掉指向本壳的重绘回调）
        //  3. 释放布局（unique_ptr 自动做）
        //
        // ⚠️ 释放布局必须在 disConnect 之后：布局及其 Dock/Panel 的重绘回调
        //    都捕获了 this（RequestRepaint），壳先死而回调还在就会野指针。
        disConnect(this);

        if (m_Layout)
            m_Layout->SetHostRepaint(nullptr);
        m_Layout.reset();
    }

    void Container::EnsureLayout()
    {
        if (m_Layout)
            return;
        m_Layout = std::make_unique<DockLayout>();
        m_Layout->SetHostRepaint([this]()
                                 { RequestRepaint(); });
    }

    void Container::SetDockLayout(DockLayout *layout)
    {
        // 释放此前的布局（若有），并先摘掉指向本壳的回调
        if (m_Layout)
        {
            m_Layout->SetHostRepaint(nullptr);
            m_Layout.reset();
        }

        m_Layout.reset(layout); // 接管所有权
        if (m_Layout)
            m_Layout->SetHostRepaint([this]()
                                     { RequestRepaint(); });

        // ★ 无条件喂尺寸（不再要求 GetNativeHandle() 非空）。
        //   setSize() 只是记下逻辑尺寸，窗口要 show() 才建 HWND；若此处因无 HWND
        //   而跳过，DockLayout 的 m_LayoutW/H 会一直是 0 → 所有 Dock 算出 0×0 矩形
        //   → 面板区高度 0、tab 栏被裁掉（"拖进 dock 就看不到 tab 栏"）。
        //   BaseWin::GetActualWidth/Height 无窗口时返回 setSize 存的逻辑值，直接可用。
        if (m_Layout)
        {
            m_Layout->SetActiveSize(static_cast<int>(get_width()),
                                    static_cast<int>(get_height()));
        }
        RequestRepaint();
    }

    Panel *Container::AddSinglePanel(Panel *panel, const std::string &title)
    {
        if (m_Layout)
            return nullptr; // 复杂场景：已有布局，不能再塞单面板

        EnsureLayout();
        Dock *dock = CreateSinglePanelDock();
        if (!dock)
            return nullptr;
        dock->SetMaxPanelCount(1); // 单面板
        // 一次到位：DockBind 内部走 AddDock，接管所有权 + 设好四条边界。
        // （不再先 AddDock 再 DockBind —— 那会重复登记一次，虽被幂等挡住，
        //   但读起来像是有意为之，实际只是历史遗留。）
        m_Layout->DockBind(*dock, InvalidBoundary, InvalidBoundary,
                           InvalidBoundary, InvalidBoundary);
        // ★ panel 的所有权交给 Dock（unique_ptr 接管）
        return dock->AddPanel(std::unique_ptr<Panel>(panel), title);
    }

    Dock *Container::CreateSinglePanelDock()
    {
        return new Dock();
    }

    std::unique_ptr<Panel> Container::DetachPanel(Panel *panel, std::string *title)
    {
        if (!m_Layout)
            return nullptr;
        for (Dock *dock : m_Layout->GetDockList())
        {
            if (!dock)
                continue;
            if (auto owned = dock->DetachPanel(panel, title))
            {
                RequestRepaint();
                return owned; // ★ 交出所有权
            }
        }
        return nullptr;
    }

    void Container::OnWindowResize()
    {
        if (m_Layout)
        {
            m_Layout->SetActiveSize(static_cast<int>(get_width()),
                                    static_cast<int>(get_height()));
        }
    }

    void Container::OnPaint(Canvas *canvas)
    {
        if (!canvas)
            return;
        if (m_Layout)
        {
            m_Layout->OnPaint(*canvas);
            return;
        }
        // 无内容：清默认背景
        canvas->Clear(0xFF202124);
    }

    void Container::OnFileDragEnter(const std::vector<XPath> &files, int x, int y)
    {
        if (m_Layout)
            m_Layout->RouteFileDragEnter(files, x, y);
    }

    void Container::OnFileDragOver(const std::vector<XPath> &files, int x, int y)
    {
        if (m_Layout)
            m_Layout->RouteFileDragOver(files, x, y);
    }

    void Container::OnFileDragLeave()
    {
        if (m_Layout)
            m_Layout->RouteFileDragLeave();
    }

    void Container::OnFileDrop(const std::vector<XPath> &files, int x, int y)
    {
        if (m_Layout)
            m_Layout->RouteFileDrop(files, x, y);
    }

} // namespace X_Y
