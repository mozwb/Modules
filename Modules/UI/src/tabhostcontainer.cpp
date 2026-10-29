#include "UI/Container/tabhostcontainer.h"
#include "UI/DockLayout/DockLayout.h"
#include "Widget/WindowHost.h"
namespace X_Y
{
    void TabHostContainer::SetDockLayout(DockLayout *layout)
    {
        Container::SetDockLayout(layout);
        ConfigureTabDocks();
    }

    void TabHostContainer::ConfigureTabDocks()
    {
        DockLayout *layout = GetDockLayout();
        if (!layout)
            return;

        for (Dock *dock : layout->GetDockList())
        {
            if (auto *tabDock = dynamic_cast<TabDock *>(dock))
                ConfigureTabDock(*tabDock);
        }
    }

    void TabHostContainer::ConfigureTabDock(TabDock &dock)
    {
        dock.SetDetachToWindowHandler(
            [this](std::unique_ptr<Panel> panel, const std::string &title, int x, int y)
            { return HandlePanelDrop(std::move(panel), title, x, y); });
    }

    bool TabHostContainer::HandlePanelDrop(std::unique_ptr<Panel> panel,
                                           const std::string &title,
                                           int, int)
    {
        if (!panel)
            return false;

        int screenX = 0;
        int screenY = 0;
        BaseWin::GetMouseScreenPos(screenX, screenY);

        // ① 优先：交给落点窗口收养
        auto *target = dynamic_cast<Container *>(
            BaseWin::GetWindowAt(screenX, screenY));
        if (target && target != this)
        {
            int targetX = screenX;
            int targetY = screenY;
            target->ScreenToClient(targetX, targetY);
            DockLayout *targetLayout = target->GetDockLayout();
            if (targetLayout)
            {
                // ⚠️ AddPanelAt 按值收 unique_ptr：失败时 panel 已被释放。
                //    所以先用裸指针记住"它还在不在"，失败就直接返回，
                //    不能再拿它去建窗口。
                if (targetLayout->AddPanelAt(std::move(panel), targetX, targetY, title))
                    return true;
                return false; // panel 已随参数析构，无法再回退建窗
            }
        }

        // ② 回退：新建一个独立窗口收容。
        //    走到这里说明还没人接管，panel 仍在本函数手里。
        //
        // ⚠️ 窗口一律 new（约定）：它的回收由 XWidget 收到 WindowDestroy 时
        //    自动完成（→ App::Application 延迟回收队列 → ProcessEvents 末尾 flush），
        //    这里【不要】再手动 delete —— 老代码两处 delete 只销毁了 HWND，
        //    C++ 对象（含它的 m_Layout→Dock→Panel 整棵树）根本没析构，反而更漏。
        auto *window = new TabContainer();
        WindowHost::Instance().Own(window);
        window->SetWindowTitle(title);
        window->setSize(700, 500);

        // AddSinglePanel 接管 panel；失败则 panel 被释放，窗口也该退场。
        if (!window->AddSinglePanel(panel.release(), title))
        {
            // destroy() → DestroyWindow → WM_DESTROY → WindowDestroy 事件
            // → XWidget 自动登记回收。本函数之后不再碰 window。
            window->destroy();
            return false;
        }

        if (!window->show())
        {
            // 窗口没建起来（Create 失败），同样走销毁 → 自动回收。
            // 此时 panel 已被 window 的 Dock 接管，随窗口一起释放。
            window->destroy();
            // panel 已随窗口处置（不算"没接管"），返回 true 与旧语义一致。
            return true;
        }

        window->MoveAndResizePhysical(screenX - 120, screenY - 20, 700, 500);
        return true;
    }
} // namespace X_Y
