#pragma once
#include "../Component/Component.h"
#include "../UiCore/UIEvent.h"
#include "../UiCore/Rect.h"
#include "Widget/Canvas.h"
#include "IO/Files.h"
#include <memory>
#include <vector>
#include <functional>
#include <cstdint>

namespace X_Y
{

    // ─────────────────────────────────────────────────────────────
    // Panel — 纯逻辑内容单元（无 HWND、无 XWidget）
    //
    // Panel 是 UI 解耦后的最小自洽内容：一棵组件树 + 它占哪块矩形 + 怎么画 +
    // 怎么把"宿主转来的输入"路由给命中的组件。
    // 它【完全不知道】宿主是谁（独立窗口 / Dock 面板 / 离屏），只认：
    //   - 宿主给的一张 Canvas&             → OnPaint(canvas)
    //   - 宿主给的布局矩形                 → SetLayoutRect(x,y,w,h)
    //   - 宿主注入的重绘回调               → SetHostRepaint(cb)
    //   - 命中路由下传的事件对象           → OnInput(UIInputEvent&)
    //
    // 宿主责任（三脚架）：
    //   1. OnPaint 里调 panel->OnPaint(canvas)
    //   2. 尺寸变化时 panel->SetLayoutRect(...)
    //   3. 命中路由(壳→DockLayout→Dock)把事件对象喂进来 → 本 Panel 内部继续
    //      命中组件并下传
    //
    // ── ⚠️ 所有权模型（2026-09-13 显式化，改代码前必读）──
    //
    //   Panel 【拥有】它挂的 Component：m_Components 是 unique_ptr 容器，
    //   析构时自动释放整棵组件树。AddComponent 即接管所有权。
    //
    //   但下面两类【借用】指针必须显式断链（它们指向组件，却不拥有）：
    //     m_FocusedComponent —— 键盘焦点目标
    //     m_DragTarget       —— 按下到抬起期间的持续交互目标
    //   RemoveComponent / ClearComponents 会清掉指向【被移除者】的借用，
    //   否则后续输入会对着已死组件调 OnInput → use-after-free。
    //
    //   还有一类"隐式借用"要留意：Component 内部可能反向持有 Panel*
    //   （如 LogViewer 的 TagStrip、ScrollArea::m_Content），
    //   这些由各组件自己在被移除时清理。
    // ─────────────────────────────────────────────────────────────
    class Panel
    {
    public:
        Panel() = default;
        virtual ~Panel() = default;

        Panel(const Panel &) = delete;
        Panel &operator=(const Panel &) = delete;

        // ── 布局：我这个面板占哪块矩形（宿主每次调整时调用）──
        void SetLayoutRect(int x, int y, int w, int h);
        void GetLayoutRect(int &x, int &y, int &w, int &h) const;
        int GetX() const { return m_X; }
        int GetY() const { return m_Y; }
        int GetWidth() const { return m_W; }
        int GetHeight() const { return m_H; }
        X_Y::Rect Rect() const { return X_Y::Rect{m_X, m_Y, m_W, m_H}; }

        // ── 组件管理 ──
        // ⚠️ AddComponent 接管所有权（存进 unique_ptr），调用方不要再 delete。
        void AddComponent(Component *comp);

        // 移除并【释放】该组件。同时清掉指向它的借用（焦点/拖拽目标）。
        void RemoveComponent(Component *comp);

        // 移除全部并释放；借用指针一律清空。
        void ClearComponents();

        // 只读视图：拿到的是借用指针，不要 delete。
        const std::vector<std::unique_ptr<Component>> &Components() const
        {
            return m_Components;
        }

        // ── 重绘请求：不调宿主窗口，而调注入的回调 ──
        void SetHostRepaint(std::function<void()> cb) { m_HostRepaint = std::move(cb); }
        void RequestRepaint()
        {
            if (m_HostRepaint)
                m_HostRepaint();
        }

        // ── 绘制：宿主递给一张画布，我把我这整棵树画上去 ──
        virtual void OnPaint(Canvas &canvas);

        // ── 输入（命中路由入口）：e.x/e.y 为相对本 Panel 的局部坐标。
        //    内部命中 m_Components（z 序），把事件对象下传给命中的组件。
        virtual void OnInput(UIInputEvent &e);

        // 文件拖拽由窗口 BaseWin 产生，这里只接收已路由到本面板的语义事件。
        virtual void OnFileDragEnter(const std::vector<XPath> &files, int x, int y)
        {
            (void)files;
            (void)x;
            (void)y;
        }
        virtual void OnFileDragOver(const std::vector<XPath> &files, int x, int y)
        {
            (void)files;
            (void)x;
            (void)y;
        }
        virtual void OnFileDragLeave() {}
        virtual void OnFileDrop(const std::vector<XPath> &files, int x, int y)
        {
            (void)files;
            (void)x;
            (void)y;
        }

        // ── 键盘焦点：向被聚焦的组件喂按键事件 ──
        void SetFocusedComponent(Component *comp);
        Component *GetFocusedComponent() const { return m_FocusedComponent; }
        void FocusNext(); // tab 顺序切换（可选，后续补）

    protected:
        // 宿主调整 Panel 区域后立即布局子组件，保证首个输入事件也能命中。
        virtual void OnLayout() {}

        // 命中测试：相对本 Panel 坐标(x,y) → 命中组件（z 序上→下，rbegin 反向）
        Component *HitTest(int x, int y);

        int m_X = 0, m_Y = 0, m_W = 100, m_H = 100;

        // ★ 拥有：Panel 独占这些组件，析构时自动释放整棵树。
        //   容器里存 unique_ptr，但对外暴露的仍是 Component*（借用）。
        std::vector<std::unique_ptr<Component>> m_Components;

        std::function<void()> m_HostRepaint;

        // ── 借用指针（不拥有，但必须随组件移除而断链，见文件头说明）──
        Component *m_FocusedComponent = nullptr; // 键盘焦点
        Component *m_DragTarget = nullptr;       // 持续交互目标（拖滑块等）
        bool m_DragTargetVisible = false;
    };

} // namespace X_Y
