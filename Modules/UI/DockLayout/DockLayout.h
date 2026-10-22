#pragma once

#include "../dock/Dock.h"
#include "../UiCore/UIEvent.h"
#include "Widget/Canvas.h"
#include "Files/Files.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <functional>

namespace X_Y
{
    // ============================================================
    // DockLayout — 纯逻辑的布局管理器（规划 Dock 布局，不再 XWidget）
    //
    // 集中式 boundary 注册表 + Dock 的父树。
    //   - 持有全局一组 Boundary（分割线），Dock 用 4 槽引用来定位。
    //   - 持有 Dock 列表，RecalcLayout 里让每个 Dock 由自己边界换算矩形。
    //   - 提供拖分割线的纯逻辑驱动（OnMouse*），由壳(Container)喂坐标。
    //   - 提供 OnPaint(Canvas&) 画背景 + 分割线 + 各 Dock。
    // 所有窗口能力（Canvas/尺寸/鼠标）由外层 Container 壳驱动，本类只算。
    //
    // ── ⚠️ 所有权模型（2026-09-13，改代码前必读）──
    //
    //   【一窗口一 layout】：DockLayout 就是窗口的布局本体，唯一。
    //   TopLayout 只是它的便利子类（预设五区域），不是"外部传进来的对象"。
    //
    //   Dock 全部归 DockLayout 所有 —— 不区分来源：
    //     · 无宗 Dock（DockFather == nullptr）—— 窗口最初始的区域划分，
    //       相对位置固定、不参与归还。
    //     · 有父 Dock（DockFather != nullptr）—— Split 切出来的，
    //       由宗族负责归还区域（Merge）。
    //   两者都在 m_Docks 里，都由本类释放。
    //
    //   ⚠️ Dock 不会"搬家"，所以不存在 Dock 层的借用概念。
    //      唯一会转移的是 Panel（在 Dock 之间搬家）—— 借用只存在于那一层。
    // ============================================================
    class DockLayout
    {
    public:
        DockLayout() = default;
        virtual ~DockLayout();

        // ── 宿主注入 ──
        void SetHostRepaint(std::function<void()> cb)
        {
            m_HostRepaint = std::move(cb);
            for (Dock *dock : m_Docks)
                if (dock)
                    dock->SetHostRepaint(m_HostRepaint);
        }
        std::function<void()> GetHostRepaint() const { return m_HostRepaint; }
        void SetActiveSize(int w, int h); // 壳的布局区域尺寸

        int GetLayoutWidth() const { return m_LayoutW; }
        int GetLayoutHeight() const { return m_LayoutH; }

        // ── boundary 注册表管理 ──
        BoundaryId AddBoundary(BoundaryOrientation orientation, float line,
                               BoundaryStatus status = BoundaryStatus::Movable);
        bool SetBoundaryColor(BoundaryId id, uint32_t color);
        bool SetBoundaryLine(BoundaryId id, float line);
        // 设置边界可移动范围。
        bool SetBoundaryRange(BoundaryId id, float min, float max);
        bool SetBoundaryWidth(BoundaryId id, int width);
        // 设置边界绘制跨度。
        bool SetBoundarySize(BoundaryId id, BoundaryId startBoundary,
                             BoundaryId endBoundary);
        bool GetBoundarySize(BoundaryId id, float &start, float &end) const;

        bool MoveBoundary(BoundaryId id, int delta);
        bool RemoveBoundary(BoundaryId id);

        const Boundary *GetBoundary(BoundaryId id) const;
        int GetBoundaryPosition(BoundaryId id) const; // 布局坐标（像素）

        // ── dock 管理 ──
        // 登记一个 Dock 并【接管所有权】（存进 unique_ptr）。
        // 无宗/有父一视同仁 —— 全部由本布局释放。
        void AddDock(Dock *dock);

        // 绑定边界并纳入布局（内部走 AddDock，同一套所有权）。
        // ⚠️ dock 以引用传入只是书写方便；所有权照样转移给本布局。
        bool DockBind(Dock &dock, BoundaryId top, BoundaryId bottom,
                      BoundaryId left, BoundaryId right);

        // 从布局中移除并【释放】。这是移除 Dock 的正常入口。
        void RemoveDock(Dock *dock);

        // 按落点收容一个 Panel（接管所有权）。
        // 落点命中哪个 Dock 就交给它；落空返回 nullptr —— 此时 panel 已被释放
        // （没有 Dock 接管），调用方若要保住它请自己持有 unique_ptr。
        Panel *AddPanelAt(std::unique_ptr<Panel> panel, int x, int y,
                          const std::string &title = "");
        const std::vector<Dock *> &GetDockList() const { return m_Docks; }

        void SetBackgroundColor(uint32_t color) { m_BackgroundColor = color; }
        uint32_t GetBackgroundColor() const { return m_BackgroundColor; }

        // 重排所有 Dock（边界变化 / 布局 resize / Dock 内容变化时调用）
        void RecalcLayout();

        // ── 命中 + 拖分割线 ──
        BoundaryId HitTestBoundary(int x, int y, int thickness = 4) const;
        // 命中哪个 Dock（布局绝对坐标）。逆序遍历，与 OnPaint 的 z 序一致。
        Dock *HitTestDock(int x, int y) const;
        bool IsDraggingBoundary() const { return m_DraggingBoundary != InvalidBoundary; }

        // ── 输入（事件对象全链路）：e.x/e.y 为相对本布局的局部坐标。
        //    先判拖分割线（鼠标 Press 命中边界→进入拖拽态，Move→拖，Release→结束）。
        //    非拖拽态：命中命中的 Dock → 下传给其激活面板。
        void RouteInput(UIInputEvent &e);

        void RouteFileDragEnter(const std::vector<XPath> &files, int x, int y);
        void RouteFileDragOver(const std::vector<XPath> &files, int x, int y);
        void RouteFileDragLeave();
        void RouteFileDrop(const std::vector<XPath> &files, int x, int y);

        // ── 全局激活面板（供壳转发输入 / 命中）──
        Panel *GetActivePanel() const;

        // ── 绘制 ──
        virtual void OnPaint(Canvas &canvas);

    protected:
        bool IsValidBoundary(BoundaryId id) const;
        int BoundaryPosition(BoundaryId id) const;

        int m_LayoutW = 0, m_LayoutH = 0;
        std::vector<Boundary> m_Boundaries;

        // ★ 拥有：Dock 全归本布局（无宗 + 有父），析构时全部释放。
        //   m_Docks 是借用视图（绘制/命中/遍历用），指向 m_OwnedDocks 里的对象。
        std::vector<std::unique_ptr<Dock>> m_OwnedDocks;
        std::vector<Dock *> m_Docks;

        uint32_t m_BackgroundColor = 0xFF202124;

        BoundaryId m_DraggingBoundary = InvalidBoundary;
        int m_LastDragX = 0, m_LastDragY = 0;
        // 鼠标按下时锁定的 Dock（从按下到抬起，事件都发给它，避免拖出边界时丢失）。
        // ⚠️ 存【指针】而不是下标：早先用 `dock - m_Docks.front()` 算下标，
        //    依赖"dock 确实在 m_Docks 里"这个前提，脆弱且易错。
        Dock *m_MouseCaptureDock = nullptr;
        Panel *m_FileDropPanel = nullptr;

        std::function<void()> m_HostRepaint;
        void RequestRepaint()
        {
            if (m_HostRepaint)
                m_HostRepaint();
        }
    };

} // namespace X_Y
