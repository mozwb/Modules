#pragma once
#include <string>
#include <memory>
#include <vector>
#include <atomic>
#include <functional>
#include "IO/Files.h"
#include "WindowImpl.h"
#include "Canvas.h"

namespace X_Y
{

    typedef unsigned int uint;

    class BaseWin
    {
    public:
        BaseWin();
        virtual ~BaseWin();

        void *GetNativeHandle() const { return m_Impl ? m_Impl->GetNativeHandle() : nullptr; }

        uint GetActualWidth() const { return m_ActualWidth; }   // 逻辑宽
        uint GetActualHeight() const { return m_ActualHeight; } // 逻辑高
        // 物理宽/高（真实像素，供系统级功能如 DockLayer 停靠）
        uint GetActualWidthPhysical() const
        {
            return m_Impl ? m_Impl->GetClientWidthPhysical() : m_ActualWidth;
        }
        uint GetActualHeightPhysical() const
        {
            return m_Impl ? m_Impl->GetClientHeightPhysical() : m_ActualHeight;
        }
        void SetActualSize(uint width, uint height)
        { // 逻辑宽高
            m_ActualWidth = width;
            m_ActualHeight = height;
        }

        // ── 跨平台工具方法 ──────────────────────────────
        // ⚠️ 坐标约定（重要，改代码前先看）：
        //   默认方法（无后缀）返回/接收【逻辑坐标】，供 UI 布局/绘制/命中测试用。
        //   需要真实像素的地方（如 DockLayer 停靠区判定、跨窗口坐标）
        //   用带 Physical 后缀的方法。上层不用自己乘/除 scale。
        // 因为内部UI设计使用得是逻辑，但是接收消息确实物理，所以补全物理到逻辑，同时留一个逻辑到物理但是估计很少用
        void GetScreenRect(int &left, int &top, int &right, int &bottom) const;
        void ScreenToClient(int &x, int &y) const;                        // 物理屏幕→逻辑客户区
        void ClientPhysicalToLogical(int &x, int &y) const;               // 物理客户区→逻辑客户区
        void ScreenToClientPhysical(int &x, int &y) const;                // 物理屏幕→物理客户区
        void ClientToScreenPhysical(int &x, int &y) const;                // 物理客户区→物理屏幕
        void GetClientRectPhysical(int &l, int &t, int &r, int &b) const; // 物理客户区

        void ClientToScreen(int &x, int &y) const; // 逻辑客户区→物理屏幕

        void CaptureMouse();
        void ReleaseMouseCapture();
        void *GetParentNativeHandle() const;
        bool SetParent(void *newParent);

        // ── 鼠标相对本窗口客户区坐标(物理) ────────
        // Get: 当前鼠标屏幕坐标 → 本窗口物理客户区相对坐标。
        //      鼠标不在本窗口内时返回 false 且 (x,y) 置负值。
        bool GetMouseClientPos(int &x, int &y) const;
        // Set: 输入本窗口物理客户区相对坐标 → 搬真实光标到该位置。
        void SetMouseClientPos(int x, int y) const;

        void SetCursorStyle(CursorStyle style);
        // 把窗口搬到物理位置
        void MoveAndResize(int x, int y, int w, int h, bool noZOrder = true);
        // 使用真实屏幕像素移动和调整窗口，不再进行 DPI 缩放。
        void MoveAndResizePhysical(int x, int y, int w, int h,
                                   bool noZOrder = true);

        // 置顶/取消置顶(运行时可切换，通用工具)

        void SetTopMost(bool topmost);

        // 分层透明属性(仅对 Layered 窗口有效，具体策略由调用方定)

        void SetLayeredAttribute(uint32_t colorKey, uint8_t alpha, uint32_t flags);

        void RequestRepaint();
        void EnableFileDrop(bool enabled = true);
        bool IsFileDropEnabled() const { return m_FileDropEnabled; }
        bool IsFileDragging() const { return m_IsFileDragging; }
        void ValidateWindow();
        void PaintDirect(std::function<void(Canvas &)> painter);

        // ── 离屏自绘（方案B：组件/逻辑画到位图，窗口只负责一次性刷新）──
        // 这套完全不走系统消息循环 / WM_PAINT，由调用方主动控制节奏。
        //   1. GetCanvas()   → 取窗口常驻离屏画布(懒创建,随窗口尺寸重建,可 Clear 后反复画)
        //   2. 往 Canvas 画  → 组件们依次 FillRect/FillTriangle/DrawText 叠到同一张位图
        //   3. Flush()       → 把整张位图一次性 BitBlt 到窗口(主动刷新)
        Canvas &GetCanvas();
        void Flush();

        // 局部上屏：把常驻离屏画布(GetCanvas)上 [x,y,w,h](逻辑)区域 BitBlt 到窗口。
        // 用于高频局部刷新(拖动分割线只翻那条窄带)，比整窗 Flush 快。
        // 配合 GetCanvas() 画完后调用；未 GetCanvas 过则 no-op。
        void FlushArea(int x, int y, int w, int h);
        void ClearBackBuffer(uint32_t color); // 清屏(填背景/抠色),画布尺寸用物理客户区

        // 主线程跳过此窗口的 WM_PAINT（用于独立线程自绘）
        std::atomic<bool> m_SkipMainThreadPaint{false};

        static void GetMouseScreenPos(int &x, int &y);

        // ── 窗口命中测试（屏幕坐标） ──────────────
        // 返回屏幕某点下，按 z 序(上→下)排列的所有窗口原生句柄。
        // 底层给全量，无论是不是 X_Y 窗口；筛选交给调用方(可用 IsXYWindow 筛查)。
        // 透明分层窗口默认不参与命中(会被跳过)，正好可用于穿透式全屏层。
        static std::vector<void *> GetWindowsAt(int screenX, int screenY);

        // 判断一个原生句柄是不是 X_Y 的 BaseWin
        static bool IsXYWindow(void *hwnd);

        static BaseWin *GetWindowAt(int screenX, int screenY);
        virtual void OnPaint(Canvas *canvas) {} // 平台绘制回调

    protected:
        virtual void OnFileDragEnter(const std::vector<XPath> &files, int x, int y) {}
        virtual void OnFileDragOver(const std::vector<XPath> &files, int x, int y) {}
        virtual void OnFileDragLeave() {}
        virtual void OnFileDrop(const std::vector<XPath> &files, int x, int y) {}

    public:
        // ── 自定义拖拽状态（由 WndProc 维护） ──────────
        bool m_IsDragging = false;
        bool m_IsFileDragging = false;
        int m_DragOffsetX = 0;
        int m_DragOffsetY = 0;

    protected:
        bool Show(ShowCmd nshow = ShowCmd::Show);
        void Close();
        void SetTitle(const char *title);
        void Destroy();
        virtual bool Create(const char *title, uint width, uint height,
                            WindowStyleFlag style = WindowStyleFlag::Overlapped,
                            void *parentHandle = nullptr);
        virtual std::string toString() const { return "BaseWindow"; }

    private:
        uint m_ActualWidth = 0;
        uint m_ActualHeight = 0;
        std::unique_ptr<WindowImpl> m_Impl;
        std::unique_ptr<Canvas> m_Canvas; // 常驻离屏画布(方案B主动绘制)
        bool m_FileDropEnabled = false;
    };

}
