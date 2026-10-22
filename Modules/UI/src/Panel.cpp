#include "UI/Panel/Panel.h"
#include "../UiCore/UINode.h"
#include <algorithm>

namespace X_Y
{

    // ── UINodeView：Panel 在 Dock 坐标系（= Dock 局部）里的矩形 ──
    UINodeView View(const Panel &node)
    {
        UINodeView v;
        v.self = Rect{ node.GetX(), node.GetY(), node.GetWidth(), node.GetHeight() };
        v.content = Rect{ 0, 0, v.self.w, v.self.h }; // 整块都是内容
        v.visible = true;
        return v;
    }

    // ── UINodeView：Component 在 Panel 坐标系（= Panel 局部）里的矩形 ──
    // Component 是叶子：没有子节点，content 即自身。
    UINodeView View(const Component &node)
    {
        UINodeView v;
        v.self = Rect{ node.GetX(), node.GetY(), node.GetWidth(), node.GetHeight() };
        v.content = Rect{ 0, 0, v.self.w, v.self.h };
        v.visible = node.IsVisible();
        return v;
    }

    // ⚠️ 坐标语义：x/y 是【宿主坐标系】里的位置 —— 由 Dock 调用时即 Dock 局部坐标。
    //    绝对坐标只活在 DockLayout/Dock 一侧，Panel 及以下全程相对。
    void Panel::SetLayoutRect(int x, int y, int w, int h)
    {
        m_X = x;
        m_Y = y;
        m_W = w;
        m_H = h;
        OnLayout();
    }

    void Panel::GetLayoutRect(int &x, int &y, int &w, int &h) const
    {
        x = m_X;
        y = m_Y;
        w = m_W;
        h = m_H;
    }

    void Panel::AddComponent(Component *comp)
    {
        if (!comp)
            return;
        comp->SetRepaintCallback([this]()
                                 { RequestRepaint(); });
        // ★ 接管所有权：调用方从此不再负责释放
        m_Components.emplace_back(comp);
    }

    void Panel::RemoveComponent(Component *comp)
    {
        if (!comp)
            return;

        // ⚠️ 顺序：先断借用指针，再释放。
        //    否则若 comp 恰好是焦点/拖拽目标，释放后它们就成悬空指针了。
        if (m_FocusedComponent == comp)
            m_FocusedComponent = nullptr;
        if (m_DragTarget == comp)
        {
            m_DragTarget = nullptr;
            m_DragTargetVisible = false;
        }

        auto it = std::find_if(m_Components.begin(), m_Components.end(),
                               [comp](const std::unique_ptr<Component> &p)
                               { return p.get() == comp; });
        if (it != m_Components.end())
            m_Components.erase(it); // unique_ptr 析构 → 释放组件
    }

    void Panel::ClearComponents()
    {
        // 全部释放；借用指针一律清空（指向的组件都没了）
        m_Components.clear();
        m_FocusedComponent = nullptr;
        m_DragTarget = nullptr;
        m_DragTargetVisible = false;
    }

    // 命中组件（z 序：后加的在上层，故 rbegin 反向遍历，最上层优先）。
    // 用共用的 Hits/UINodeView 判定，与 Panel::OnPaint 的绘制遍历同源。
    Component *Panel::HitTest(int x, int y)
    {
        for (auto it = m_Components.rbegin(); it != m_Components.rend(); ++it)
        {
            Component *comp = it->get();
            if (!comp->IsVisible())
                continue;
            if (View(*comp).self.Contains(x, y))
                return comp;
        }
        return nullptr;
    }

    void Panel::SetFocusedComponent(Component *comp)
    {
        if (m_FocusedComponent == comp)
            return;

        if (m_FocusedComponent)
            m_FocusedComponent->SetFocused(false);
        m_FocusedComponent = comp;
        if (m_FocusedComponent)
            m_FocusedComponent->SetFocused(true);
        RequestRepaint();
    }

    // ── 输入命中路由：把事件对象下传给命中的组件（z 序），支持 Handled 冒泡 ──
    // ★ 几何来源：View(*this) / View(*comp)，与 Panel::OnPaint 共用同一份描述。
    void Panel::OnInput(UIInputEvent &e)
    {
        // 鼠标类事件：命中组件，下传
        if (auto *me = dynamic_cast<UIMouseEvent *>(&e))
        {
            // 持续交互（拖滑块/press 后 move/up）：优先交给 m_DragTarget
            Component *target = (m_DragTarget && m_DragTargetVisible) ? m_DragTarget : nullptr;
            if (!target)
                target = HitTest(e.x, e.y);

            if (target)
            {
                if (me->action == MouseAction::Press)
                {
                    // 按下：设焦点 + 记拖拽目标
                    SetFocusedComponent(target);
                    m_DragTarget = target;
                    m_DragTargetVisible = true;
                }
                else if (me->action == MouseAction::Release)
                {
                    m_DragTarget = nullptr;
                    m_DragTargetVisible = false;
                }

                // 下钻到组件：e 是 Panel 局部坐标，View(Component).self 正是
                // 组件在 Panel 坐标系里的位置 —— 直接用共用原语转换。
                // （不再手写 e.x -= cx / e.y -= cy）
                const UINodeView cv = View(*target);
                ToLocal(cv, e.x, e.y);
                target->OnInput(e);
                ToParent(cv, e.x, e.y); // 还原成 Panel 局部，供上层保持视角
            }
            else if (me->action == MouseAction::Press)
            {
                SetFocusedComponent(nullptr); // 点空白 → 清焦点
            }
            return;
        }

        // 键盘焦点：喂给被聚焦的组件
        if (auto *ke = dynamic_cast<UIKeyEvent *>(&e))
        {
            if (m_FocusedComponent)
            {
                m_FocusedComponent->OnInput(e);
            }
            return;
        }
    }

    // Panel 把自己这棵树画到 canvas 上。
    // ★ 坐标约定：调用方（Dock）已 PushOrigin 到 Panel 的位置，所以本函数
    //   及所有组件一律按【Panel 局部坐标】(0,0 起画)。
    // ★ 几何来源：走 View(*this) / View(*comp)，与 Panel::OnInput 的命中
    //   共用同一份节点描述（绘制与路由同构）。
    void Panel::OnPaint(Canvas &canvas)
    {
        const UINodeView panelView = View(*this);

        // 外层裁剪：整个 Panel 的内容不许画到自己矩形之外。
        canvas.SetClip(0, 0, panelView.content.w, panelView.content.h);

        for (const auto &owned : m_Components)
        {
            Component *comp = owned.get();
            if (!comp->IsVisible())
                continue;

            // 每个组件压自己的 origin：组件内部一律按 (0,0) 起画，
            // 位置由宿主提供（与 Dock 对 Panel 的做法一致）。
            const UINodeView cv = View(*comp);
            canvas.PushOrigin(cv.self.x, cv.self.y);
            canvas.SetClip(0, 0, cv.self.w, cv.self.h);
            comp->OnPaint(canvas);
            canvas.ResetClip();
            canvas.PopOrigin();
            canvas.SetClip(0, 0, panelView.content.w, panelView.content.h); // 恢复外层裁剪
        }

        canvas.ResetClip();
    }

    void Panel::FocusNext()
    {
        // 简易 tab 顺序：按添加顺序找下一个可见组件
        if (m_Components.empty())
            return;
        int start = 0;
        if (m_FocusedComponent)
        {
            auto it = std::find_if(m_Components.begin(), m_Components.end(),
                                   [this](const std::unique_ptr<Component> &p)
                                   { return p.get() == m_FocusedComponent; });
            if (it != m_Components.end())
                start = (int)(it - m_Components.begin()) + 1;
        }
        const int n = (int)m_Components.size();
        for (int k = 0; k < n; ++k)
        {
            Component *c = m_Components[(start + k) % n].get();
            if (c->IsVisible())
            {
                SetFocusedComponent(c);
                return;
            }
        }
    }

} // namespace X_Y
