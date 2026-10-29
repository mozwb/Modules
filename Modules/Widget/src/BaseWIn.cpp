#include "Widget/BaseWin.h"
#include "Widget/Canvas.h"
#include "Input/Input.h"
#include "Widget/Dpi.h"
#include <set>
#ifdef XY_PLATFORM_WINDOWS
#include <windows.h>
#include "Win32/Win32Class.h"
#include "Win32/Win32Globals.h"
#endif

namespace X_Y
{

    BaseWin::BaseWin()
        : m_Impl(PlatformFactory::CreateWindowImpl())
    {
#ifdef XY_PLATFORM_WINDOWS
        // 窗口类 / DPI 只需要进程级初始化一次。以前依赖 Entry.cpp 的
        // WinMain 或 Widget/PlatformLoop 来做，现在 BaseWin 自己兜底：
        // 控制台程序直接 new XWidget 也能建窗。
        static bool s_PlatformBooted = false;
        if (!s_PlatformBooted)
        {
            Dpi::DeclareAware();
            Win32::RegisterWinClass(::GetModuleHandle(nullptr));
            s_PlatformBooted = true;
        }
#endif
    }

    BaseWin::~BaseWin()
    {
        Destroy();
    }

    // ── 窗口生命周期 ──────────────────────────────

    bool BaseWin::Create(const char *title, uint width, uint height,
                         WindowStyleFlag style, void *parentHandle)
    {
        if (!m_Impl)
            return false;
        bool created = m_Impl->Create(title, width, height, style, parentHandle, this);
        if (created && m_FileDropEnabled)
            EnableFileDrop(true);
        return created;
    }

    bool BaseWin::Show(ShowCmd nshow)
    {
        return m_Impl ? m_Impl->Show(nshow) : false;
    }

    void BaseWin::Close()
    {
        if (m_Impl)
            m_Impl->Close();
    }

    void BaseWin::Destroy()
    {
        if (!m_Impl)
            return;
        m_Impl->EnableFileDrop(false, {});
        m_IsFileDragging = false;
        m_Impl->Destroy();
        m_Impl.reset(); // 防止重复 Destroy
    }

    void BaseWin::EnableFileDrop(bool enabled)
    {
        m_FileDropEnabled = enabled;
        if (!m_Impl)
            return;
        WindowImpl::FileDropCallbacks callbacks;
        callbacks.onEnter = [this](const std::vector<XPath> &files, int x, int y)
        {
            m_IsFileDragging = true;
            OnFileDragEnter(files, x, y);
        };
        callbacks.onOver = [this](const std::vector<XPath> &files, int x, int y)
        {
            OnFileDragOver(files, x, y);
        };
        callbacks.onLeave = [this]()
        {
            OnFileDragLeave();
            m_IsFileDragging = false;
        };
        callbacks.onDrop = [this](const std::vector<XPath> &files, int x, int y)
        {
            OnFileDrop(files, x, y);
            m_IsFileDragging = false;
        };
        m_Impl->EnableFileDrop(enabled, std::move(callbacks));
    }

    void BaseWin::SetTitle(const char *title)
    {
        if (m_Impl)
            m_Impl->SetTitle(title);
    }

    // ── 窗口信息 ──────────────────────────────────

    void BaseWin::GetScreenRect(int &left, int &top, int &right, int &bottom) const
    {
        if (m_Impl)
            m_Impl->GetClientRect(left, top, right, bottom);
    }

    void *BaseWin::GetParentNativeHandle() const
    {
        if (m_Impl)
            return m_Impl->GetParentNativeHandle();
        return nullptr;
    }

    bool BaseWin::SetParent(void *newParent)
    {
        return m_Impl ? m_Impl->SetParent(newParent) : false;
    }

    // ── 坐标转换 ──────────────────────────────────

    void BaseWin::ScreenToClient(int &x, int &y) const
    {
        if (m_Impl)
            m_Impl->ScreenToClient(x, y);
    }

    void BaseWin::ClientPhysicalToLogical(int &x, int &y) const
    {
        const float scale = Dpi::GetScale();
        x = static_cast<int>(x / scale);
        y = static_cast<int>(y / scale);
    }

    void BaseWin::ClientToScreen(int &x, int &y) const
    {
        if (m_Impl)
            m_Impl->ClientToScreen(x, y);
    }

    void BaseWin::ScreenToClientPhysical(int &x, int &y) const
    {
        if (m_Impl)
            m_Impl->ScreenToClientPhysical(x, y);
    }

    void BaseWin::ClientToScreenPhysical(int &x, int &y) const
    {
        if (m_Impl)
            m_Impl->ClientToScreenPhysical(x, y);
    }

    void BaseWin::GetClientRectPhysical(int &l, int &t, int &r, int &b) const
    {
        if (m_Impl)
            m_Impl->GetClientRectPhysical(l, t, r, b);
    }

    // ── 鼠标相对本窗口客户区坐标(物理) ──────────────

    bool BaseWin::GetMouseClientPos(int &x, int &y) const
    {
        x = y = -1; // 默认：不在窗口内
        int sx, sy;
        GetMouseScreenPos(sx, sy);      // 物理屏幕坐标
        ScreenToClientPhysical(sx, sy); // → 物理客户区
        // 判断是否落在本窗口客户区矩形内(物理坐标)
        int l, t, r, b;
        GetClientRectPhysical(l, t, r, b);
        if (sx >= l && sx < r && sy >= t && sy < b)
        {
            x = sx;
            y = sy;
            return true;
        }
        return false;
    }

    void BaseWin::SetMouseClientPos(int x, int y) const
    {
        // 物理客户区 → 物理屏幕(全局坐标)
        ClientToScreenPhysical(x, y);
        // 复用 Input 层搬光标，不在此重复封装 Win32 API
        Input::SetMousePosition((float)x, (float)y);
    }

    // ── 鼠标 & 光标 ───────────────────────────────

    void BaseWin::CaptureMouse()
    {
        if (m_Impl)
            m_Impl->CaptureMouse();
    }

    void BaseWin::ReleaseMouseCapture()
    {
        if (m_Impl)
            m_Impl->ReleaseMouseCapture();
    }

    void BaseWin::SetCursorStyle(CursorStyle style)
    {
        if (m_Impl)
            m_Impl->SetCursorStyle(style);
    }

    void BaseWin::SetTopMost(bool topmost)
    {
        if (m_Impl)
            m_Impl->SetTopMost(topmost);
    }

    void BaseWin::SetLayeredAttribute(uint32_t colorKey, uint8_t alpha, uint32_t flags)
    {
        if (m_Impl)
            m_Impl->SetLayeredAttribute(colorKey, alpha, flags);
    }

    void BaseWin::MoveAndResize(int x, int y, int w, int h, bool noZOrder)
    {
        if (m_Impl)
            m_Impl->MoveAndResize(x, y, w, h, noZOrder);
    }

    void BaseWin::MoveAndResizePhysical(int x, int y, int w, int h,
                                        bool noZOrder)
    {
        if (m_Impl)
            m_Impl->MoveAndResizePhysical(x, y, w, h, noZOrder);
    }

    // ── 离屏自绘(方案B)：窗口常驻画布 + 主动刷新，不走系统循环 ──
    // 组件们 GetCanvas() 往同一张位图上叠画，最后 Flush() 一次上屏。

    Canvas &BaseWin::GetCanvas()
    {
        // 用窗口客户区物理像素作为画布尺寸(离屏位图分辨率)
        int cw = (int)GetActualWidthPhysical();
        int ch = (int)GetActualHeightPhysical();
        if (cw < 1 || ch < 1)
            cw = 1, ch = 1;

        if (m_Canvas)
        {
            // 尺寸变了(窗口 resize) → 重建画布（画布内部存物理缓冲，按物理尺寸比较）
            if (m_Canvas->GetPhysicalWidth() != cw ||
                m_Canvas->GetPhysicalHeight() != ch)
            {
                m_Canvas.reset();
            }
            else
            {
                return *m_Canvas;
            }
        }

#ifdef XY_PLATFORM_WINDOWS
        // Canvas 构造接收窗口句柄(HWND)，内部 Flush 时动态 GetDC/ReleaseDC，常驻安全
        m_Canvas.reset(new Canvas(cw, ch, GetNativeHandle()));
#else
        m_Canvas.reset(new Canvas(cw, ch, nullptr));
#endif
        return *m_Canvas;
    }

    void BaseWin::Flush()
    {
        if (m_Canvas)
            m_Canvas->Flush();
    }

    void BaseWin::FlushArea(int x, int y, int w, int h)
    {
        if (m_Canvas)
            m_Canvas->FlushRect(x, y, w, h);
    }

    void BaseWin::ClearBackBuffer(uint32_t color)
    {
        Canvas &c = GetCanvas();
        // 整幅物理尺寸清屏填色（软件后端 Clear 带 alpha 填充）
        c.Clear(color);
    }

    void BaseWin::RequestRepaint()
    {
        if (m_Impl)
            m_Impl->RequestRepaint();
    }

    void BaseWin::ValidateWindow()
    {
        if (m_Impl)
            m_Impl->ValidateWindow();
    }

    void BaseWin::PaintDirect(std::function<void(Canvas &)> painter)
    {
        if (m_Impl)
            m_Impl->PaintDirect(std::move(painter));
    }

    void BaseWin::GetMouseScreenPos(int &x, int &y)
    {
        WindowImpl::GetMouseScreenPos(x, y);
    }

    BaseWin *BaseWin::GetWindowAt(int screenX, int screenY)
    {
#ifdef XY_PLATFORM_WINDOWS
        POINT pt = {screenX, screenY};
        HWND hwnd = ::WindowFromPoint(pt);
        if (hwnd)
            hwnd = ::GetAncestor(hwnd, GA_ROOT);
        if (!hwnd)
            return nullptr;

        TCHAR className[256] = {};
        if (::GetClassName(hwnd, className, 256) == 0 ||
            ::lstrcmp(className, Win32::g_szClassName) != 0)
            return nullptr;

        return (BaseWin *)::GetWindowLongPtr(hwnd, GWLP_USERDATA);
#else
        return nullptr;
#endif
    }

    bool BaseWin::IsXYWindow(void *hwnd)
    {
#ifdef XY_PLATFORM_WINDOWS
        if (!hwnd)
            return false;
        HWND h = (HWND)hwnd;
        // 实时查 (而非直接读 GWLP_USERDATA)：防止窗口已被销毁但句柄仍被外部持有
        if (!::IsWindow(h))
            return false;
        return ::GetWindowLongPtr(h, GWLP_USERDATA) != 0;
#else
        return false;
#endif
    }

    std::vector<void *> BaseWin::GetWindowsAt(int screenX, int screenY)
    {
        std::vector<void *> result;
#ifdef XY_PLATFORM_WINDOWS
        POINT pt = {screenX, screenY};
        // 最顶层的窗口(可能含子窗口：WindowFromPoint 默认只在同线程/子窗口间的顶层生效，
        // 这里用 GetAncestor 归一化到顶层窗口，保证列表是顶层 z 序)。
        HWND top = ::GetAncestor(::WindowFromPoint(pt), GA_ROOT);
        if (!top)
            return result;
        // 记录已处理句柄，防同一窗口经不同路径重复出现
        std::set<HWND> seen;
        for (HWND h = top; h; h = ::GetNextWindow(h, GW_HWNDNEXT))
        {
            if (seen.count(h))
                break;
            seen.insert(h);
            // 只收“包含该点”的窗口(在屏幕坐标矩形内)
            RECT rc;
            if (::GetWindowRect(h, &rc))
            {
                bool pointsInside = pt.x >= rc.left && pt.x < rc.right &&
                                    pt.y >= rc.top && pt.y < rc.bottom;
                if (pointsInside)
                    result.push_back((void *)h);
            }
        }
#else
        (void)screenX;
        (void)screenY;
#endif
        return result;
    }

} // namespace X_Y
