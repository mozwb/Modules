#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include "Files/Files.h"

// Canvas 前向声明（PaintDirect 需要）
namespace X_Y
{
    class Canvas;
}

namespace X_Y
{

    // 窗口显示方式（跨平台抽象）

    enum class ShowCmd
    {
        Hide,
        Normal,
        Minimized,
        Maximized,
        Show,
        Default
    };

    // 光标样式

    enum class CursorStyle
    {
        Arrow,
        SizeWE,
        SizeNS,
        SizeAll,
        Hand,
        IBeam,
        None // 隐藏光标(SetCursor(nullptr))
    };

    // 窗口风格（位标志，平台内部转换）

    enum class WindowStyleFlag : uint32_t
    {
        None = 0,
        Overlapped = 1 << 0,
        Child = 1 << 1,
        Popup = 1 << 2,
        Borderless = 1 << 3,
        Resizable = 1 << 4,
        ClipChildren = 1 << 5,
        ClipSiblings = 1 << 6,
        Visible = 1 << 7,
        TopMost = 1 << 8, // 置顶(WS_EX_TOPMOST)
        Layered = 1 << 9, // 分层透明机制(WS_EX_LAYERED)，具体透明策略见 SetLayeredAttribute
    };

    inline WindowStyleFlag operator|(WindowStyleFlag a, WindowStyleFlag b)
    {
        return static_cast<WindowStyleFlag>(
            static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline bool HasFlag(WindowStyleFlag value, WindowStyleFlag flag)
    {
        return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
    }

    // 平台窗口实现——纯虚接口
    // 不依赖 BaseWin，只操作原生窗口句柄

    class WindowImpl
    {
    public:
        struct FileDropCallbacks
        {
            std::function<void(const std::vector<XPath> &, int, int)> onEnter;
            std::function<void(const std::vector<XPath> &, int, int)> onOver;
            std::function<void()> onLeave;
            std::function<void(const std::vector<XPath> &, int, int)> onDrop;
        };

        virtual ~WindowImpl() = default;

        // ── 窗口生命周期 ──────────────────────────

        virtual bool Create(const char *title, uint32_t width, uint32_t height,
                            WindowStyleFlag style, void *parentHandle,
                            void *createParam) = 0;
        virtual bool Show(ShowCmd cmd) = 0;
        virtual void Close() = 0;
        virtual void Destroy() = 0;
        virtual void SetTitle(const char *title) = 0;

        // ── 原生句柄 ──────────────────────────────

        virtual void *GetNativeHandle() const = 0;
        virtual void *GetParentNativeHandle() const = 0;

        // ── 窗口父子关系 ──────────────────────────

        virtual bool SetParent(void *newParent) = 0;

        // ── 窗口尺寸 ──────────────────────────────
        // ⚠️ 下列方法默认返回/接收【逻辑坐标】（已按 DPI ÷scale）。
        //   物理版本用带 Physical 后缀的接口。

        virtual void GetClientRect(int &left, int &top, int &right, int &bottom) const = 0; // 逻辑
        virtual uint32_t GetClientWidth() const = 0;                                        // 逻辑
        virtual uint32_t GetClientHeight() const = 0;                                       // 逻辑
        virtual void SetClientSize(uint32_t width, uint32_t height) = 0;                    // 逻辑宽高

        // ── 窗口尺寸（物理像素，供需要真实像素的系统级功能使用）──
        virtual void GetClientRectPhysical(int &left, int &top, int &right, int &bottom) const = 0;
        virtual uint32_t GetClientWidthPhysical() const = 0;
        virtual uint32_t GetClientHeightPhysical() const = 0;

        // ── 坐标转换 ──────────────────────────────
        // ScreenToClient / ClientToScreen 默认【逻辑】，物理版带 Physical 后缀。
        // 注意：滚动派生量（滚轮 delta）不是经此接口，需调用方按需 ÷scale。

        virtual void ScreenToClient(int &x, int &y) const = 0;         // 输出逻辑
        virtual void ClientToScreen(int &x, int &y) const = 0;         // 输入逻辑
        virtual void ScreenToClientPhysical(int &x, int &y) const = 0; // 输出物理
        virtual void ClientToScreenPhysical(int &x, int &y) const = 0; // 输入物理

        // ── 重绘请求 ──────────────────────────────

        virtual void RequestRepaint() = 0;
        virtual void ValidateWindow() = 0;

        // ── 自绘（线程安全：调用方线程 GetDC，完事 ReleaseDC）──
        // 在任意线程调用；callback 拿到 Canvas 画即可

        virtual void PaintDirect(std::function<void(Canvas &)> painter) = 0;

        // ── 鼠标 & 光标 ───────────────────────────

        virtual void CaptureMouse() = 0;
        virtual void ReleaseMouseCapture() = 0;
        virtual void SetCursorStyle(CursorStyle style) = 0;

        // ── 置顶(运行时可切换) ────────────────────
        // 通用工具：任何 BaseWin 都能随时置顶/取消，不需重建窗口。

        virtual void SetTopMost(bool topmost) = 0;

        // ── 分层透明属性(机械转发，不掺语义) ──────
        // 底层只负责把"透明参数"传给窗口，具体抠哪个色/用哪种透明是上层策略。
        // 仅当窗口以 WindowStyleFlag::Layered 创建后调用才有效。
        //   flags: LWA_COLORKEY(按 colorKey 抠色) / LWA_ALPHA(整窗 alpha) 等
        //   colorKey/alpha 具体含义由 flags 决定，未用到的传 0。

        virtual void SetLayeredAttribute(uint32_t colorKey, uint8_t alpha, uint32_t flags) = 0;

        virtual void EnableFileDrop(bool enabled, FileDropCallbacks callbacks) = 0;
        virtual bool IsFileDragging() const = 0;

        // ── 静态工具（全局操作，无需实例） ────────

        static void GetMouseScreenPos(int &x, int &y);
        static void ReleaseGlobalMouseCapture();
        virtual void MoveAndResize(int x, int y, int w, int h, bool noZOrder) = 0;
        virtual void MoveAndResizePhysical(int x, int y, int w, int h,
                                           bool noZOrder) = 0;
    };

    // 平台工厂

    class PlatformFactory
    {
    public:
        static WindowImpl *CreateWindowImpl();
    };

}
