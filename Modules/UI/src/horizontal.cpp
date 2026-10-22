#include "UI/Component/horizontal.h"
#include "../UiCore/UINode.h"
#include <algorithm>

namespace X_Y
{
    void Horizontal::AddComponent(Component *component)
    {
        if (!component)
            return;

        component->SetRepaintCallback([this]()
                                      { RequestRepaint(); });
        Components.push_back(component);
        LayoutComponents();
        RequestRepaint();
    }

    void Horizontal::AddComponents(std::initializer_list<Component *> components)
    {
        for (Component *component : components)
            AddComponent(component);
    }

    void Horizontal::RemoveComponent(Component *component)
    {
        auto it = std::find(Components.begin(), Components.end(), component);
        if (it == Components.end())
            return;

        const std::size_t removedIndex = static_cast<std::size_t>(it - Components.begin());
        Components.erase(it);
        if (SelectedIndex == removedIndex)
            SelectedIndex = npos;
        else if (SelectedIndex != npos && SelectedIndex > removedIndex)
            --SelectedIndex;

        LayoutComponents();
        RequestRepaint();
    }

    void Horizontal::Clear()
    {
        Components.clear();
        SelectedIndex = npos;
        RequestRepaint();
    }

    void Horizontal::SetGap(int gap)
    {
        Gap = std::max(0, gap);
        LayoutComponents();
        RequestRepaint();
    }

    Component *Horizontal::GetComponent(std::size_t index) const
    {
        return index < Components.size() ? Components[index] : nullptr;
    }

    void Horizontal::Select(std::size_t index)
    {
        if (index >= Components.size())
            return;

        SelectedIndex = index;
        for (std::size_t i = 0; i < Components.size(); ++i)
            Components[i]->SetFocused(i == SelectedIndex);

        if (OnSelected)
            OnSelected(index, Components[index]);
        RequestRepaint();
    }

    void Horizontal::LayoutComponents()
    {
        int x = 0;
        for (Component *component : Components)
        {
            component->SetRect(x, 0, component->GetWidth(), GetHeight());
            x += component->GetWidth() + Gap;
        }
    }

    Component *Horizontal::HitTest(int x, int y, std::size_t &index) const
    {
        for (std::size_t i = 0; i < Components.size(); ++i)
        {
            Component *component = Components[i];
            if (component->IsVisible() && x >= component->GetX() &&
                x < component->GetX() + component->GetWidth() &&
                y >= component->GetY() && y < component->GetY() + component->GetHeight())
            {
                index = i;
                return component;
            }
        }
        index = npos;
        return nullptr;
    }

    void Horizontal::OnPaint(Canvas &canvas)
    {
        LayoutComponents();
        for (Component *component : Components)
        {
            if (!component->IsVisible())
                continue;

            // 子组件按自己的 (0,0) 起画：压入它在自身局部坐标系里的位置
            const UINodeView cv = View(*component);
            canvas.PushOrigin(cv.self.x, cv.self.y);
            canvas.SetClip(0, 0, cv.self.w, cv.self.h);
            component->OnPaint(canvas);
            canvas.ResetClip();
            canvas.PopOrigin();
        }
    }

    void Horizontal::OnInput(UIInputEvent &e)
    {
        // 键盘：转发给聚焦的子组件
        if (auto *ke = dynamic_cast<UIKeyEvent *>(&e))
        {
            for (Component *component : Components)
                if (component->IsVisible() && component->IsFocused())
                {
                    component->OnInput(e);
                    return;
                }
            return;
        }

        auto *me = dynamic_cast<UIMouseEvent *>(&e);
        if (!me)
            return;

        std::size_t index = npos;
        Component *component = HitTest(e.x, e.y, index);
        if (!component)
            return;

        if (me->action == MouseAction::Press)
            Select(index);

        // 用共用原语下钻（与 OnPaint 的 PushOrigin 同源：都取 View(comp).self）
        const UINodeView cv = View(*component);
        ToLocal(cv, e.x, e.y);
        component->OnInput(e);
        ToParent(cv, e.x, e.y);
    }
}