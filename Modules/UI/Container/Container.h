#pragma once
#include "Widget/XWidget.h"
#include "Widget/Canvas.h"
#include "Files/Files.h"
#include <memory>
#include <string>

namespace X_Y
{

    class Panel;
    class Dock;
    class DockLayout;

    // Container — 壳（ShellWidget）：一个带 HWND 的窗口，统一持有 DockLayout。
    //
    // 单一模型：Container 永远持有一个 DockLayout（纯逻辑布局）。DockLayout 规划
    // 若干 Dock（不同宗），Dock 持有多个 Panel。因此：
    //   - 独立工具窗口  = Container + DockLayout + 一个 Dock + 一个 Panel
    //   - Dock 宿主窗口 = Container + DockLayout + 多个 Dock + 各 Dock 的多个 Panel
    // 两者只是 DockLayout 里装了多少内容的差别，Container 永远是同一个壳。
    //
    // Container 只干三件事（壳的职责）：
    //   1. 提供 Canvas（XWidget 自带）→ 转给 DockLayout::OnPaint
    //   2. 提供尺寸（客户区）→ DockLayout::SetActiveSize
    //   3. 转发输入（Movement）→ 转布局坐标 → DockLayout::OnMouse*
    //
    // Container 自身【不再管理组件】——组件只能在 Panel 里。DockLayout/Dock 也只
    // 是宿主，不含组件。HexViewer/LogViewer 等旧 composite 后续迁到 Panel 形态。
    //
    // ── ⚠️ 所有权模型（2026-09-13，改代码前必读）──
    //
    //   【一窗口一 layout】：Container 拥有唯一一个 DockLayout。
    //   TopLayout 只是 DockLayout 的便利子类（预设五区域），
    //   它【就是这个窗口的 layout】，不是"外部传进来的第三方对象"。
    //   所以这里只有一个 unique_ptr，没有"自建/外部"之分。
    // ─────────────────────────────────────────────────────────────
    class Container : public XWidget
    {
    public:
        // ⚠️ storage 默认 Heap：关窗时 XWidget 会自毁（delete this）。
        //    栈上建壳子要显式传 StorageTag::Stack —— 见 XWidget.h 的 StorageTag 说明。
        //    （TabContainer / TabHostContainer 用 `using Container::Container`
        //      继承这个构造，所以它们自动支持同样的写法。）
        explicit Container(XWidget *parent = nullptr,
                           StorageTag storage = StorageTag::Heap);
        ~Container() override;

        // ── 统一模型：总是挂一个纯逻辑 DockLayout ──
        // ⚠️ 接管所有权：传入的 layout 由本壳负责释放。
        virtual void SetDockLayout(DockLayout *layout); // 复杂场景：塞整个布局

        // 便捷：一行造一个"单面板工具窗"。内部自动 DockLayout → Dock → Panel。
        // ⚠️ panel 的所有权交给 Dock（由 Dock 的 unique_ptr 持有），
        //    调用方不要再 delete 它。
        virtual Panel *AddSinglePanel(Panel *panel, const std::string &title = "");

        DockLayout *GetDockLayout() const { return m_Layout.get(); }

        // 让渡：把一个 Panel 交出去（拖进别的 Dock / 摘成独立窗）
        // ★ 返回 unique_ptr = 交出所有权；调用方拿到即拥有。
        //   若在接管前析构（早退/抛异常），Panel 会被正确释放，不会泄漏。
        std::unique_ptr<Panel> DetachPanel(Panel *panel, std::string *title = nullptr);

    protected:
        virtual Dock *CreateSinglePanelDock();

        void OnPaint(Canvas *canvas) override; // 壳把画布交给 DockLayout
        void OnFileDragEnter(const std::vector<XPath> &files, int x, int y) override;
        void OnFileDragOver(const std::vector<XPath> &files, int x, int y) override;
        void OnFileDragLeave() override;
        void OnFileDrop(const std::vector<XPath> &files, int x, int y) override;

    private:
        // 尺寸/输入转发（由 WndProc 消息经 XWidget 回调触发）
        void OnWindowResize();
        void EnsureLayout(); // 懒建默认 DockLayout

        // ★ 拥有：本壳唯一的布局。析构自动释放。
        std::unique_ptr<DockLayout> m_Layout;
    };

} // namespace X_Y
