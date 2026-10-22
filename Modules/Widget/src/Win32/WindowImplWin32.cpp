#include "Win32/WindowImplWin32.h"
#include "Win32/Win32Globals.h"
#include "Widget/Canvas.h"
#include "Widget/Dpi.h"
#include <windows.h>
#include <ole2.h>
#include <shlobj.h>
#include <cstdint>

namespace X_Y
{

    // ── 窗口风格转换 ──────────────────────────────────────
    static DWORD TranslateWindowStyle(WindowStyleFlag style)
    {
        DWORD ws = 0;
        if (HasFlag(style, WindowStyleFlag::Overlapped))
            ws |= WS_OVERLAPPEDWINDOW;
        if (HasFlag(style, WindowStyleFlag::Child))
            ws |= WS_CHILD;
        if (HasFlag(style, WindowStyleFlag::Popup))
            ws |= WS_POPUP;
        if (HasFlag(style, WindowStyleFlag::Borderless))
            ws |= WS_POPUP;
        if (HasFlag(style, WindowStyleFlag::Resizable))
            ws |= WS_SIZEBOX;
        if (HasFlag(style, WindowStyleFlag::ClipChildren))
            ws |= WS_CLIPCHILDREN;
        if (HasFlag(style, WindowStyleFlag::ClipSiblings))
            ws |= WS_CLIPSIBLINGS;
        if (HasFlag(style, WindowStyleFlag::Visible))
            ws |= WS_VISIBLE;
        // 默认窗口：如果没有指定任何风格，给 Overlapped
        if (ws == 0)
            ws = WS_OVERLAPPEDWINDOW;
        return ws;
    }

    // ── 扩展风格转换(WS_EX_*) ─────────────────────────────
    static DWORD TranslateExStyle(WindowStyleFlag style)
    {
        DWORD ex = 0;
        if (HasFlag(style, WindowStyleFlag::TopMost))
            ex |= WS_EX_TOPMOST;
        if (HasFlag(style, WindowStyleFlag::Layered))
            ex |= WS_EX_LAYERED;
        return ex;
    }

    class WindowImplWin32 : public WindowImpl
    {
    public:
        WindowImplWin32() : m_Hwnd(nullptr) {}
        ~WindowImplWin32() override;

        bool Create(const char *title, uint32_t width, uint32_t height,
                    WindowStyleFlag style, void *parentHandle,
                    void *createParam) override
        {
            if (width == 0)
                width = 800;
            if (height == 0)
                height = 600;

            // 逻辑尺寸 → 物理像素（DPI-aware 后窗口不再自动缩放）
            float s = Dpi::GetScale();
            width = (uint32_t)(width * s);
            height = (uint32_t)(height * s);

            wchar_t wTitle[256] = {0};
            // 标题按 UTF-8 转宽（全链路统一 UTF-8）
            MultiByteToWideChar(CP_UTF8, 0, title, -1, wTitle, _countof(wTitle));

            DWORD dwStyle = TranslateWindowStyle(style);
            DWORD dwExStyle = TranslateExStyle(style);

            int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
            if (dwStyle & WS_CHILD)
            {
                x = 0;
                y = 0;
            }

            HWND hwnd = CreateWindowEx(
                dwExStyle, Win32::g_szClassName, wTitle,
                dwStyle, x, y, width, height,
                (HWND)parentHandle, nullptr,
                Win32::g_hInstance,
                createParam);

            if (!hwnd)
                return false;

            m_Hwnd = hwnd;
            return true;
        }

        bool Show(ShowCmd cmd) override
        {
            if (!m_Hwnd)
                return false;
            static const int nShowMap[] = {
                SW_HIDE, SW_NORMAL, SW_MINIMIZE, SW_MAXIMIZE, SW_SHOW, SW_SHOWDEFAULT};
            int idx = static_cast<int>(cmd);
            ShowWindow(m_Hwnd, (idx >= 0 && idx < 6) ? nShowMap[idx] : SW_SHOW);
            UpdateWindow(m_Hwnd);
            return true;
        }

        void Close() override
        {
            if (m_Hwnd)
                Show(ShowCmd::Hide);
        }

        void Destroy() override
        {
            if (m_Hwnd)
            {
                HWND h = m_Hwnd;
                m_Hwnd = nullptr;
                DestroyWindow(h);
            }
        }

        void SetTitle(const char *title) override
        {
            if (m_Hwnd)
                SetWindowTextA(m_Hwnd, title);
        }

        void *GetNativeHandle() const override { return m_Hwnd; }
        void *GetParentNativeHandle() const override
        {
            return m_Hwnd ? ::GetParent(m_Hwnd) : nullptr;
        }

        bool SetParent(void *newParent) override
        {
            if (!m_Hwnd)
                return false;
            ::SetParent(m_Hwnd, (HWND)newParent);
            return true;
        }

        void GetClientRect(int &l, int &t, int &r, int &b) const override
        { // 逻辑
            if (!m_Hwnd)
            {
                l = t = r = b = 0;
                return;
            }
            RECT rc;
            ::GetClientRect(m_Hwnd, &rc);
            float s = Dpi::GetScale();
            l = (int)(rc.left / s);
            t = (int)(rc.top / s);
            r = (int)(rc.right / s);
            b = (int)(rc.bottom / s);
        }

        uint32_t GetClientWidth() const override
        { // 逻辑
            int l, t, r, b;
            GetClientRect(l, t, r, b);
            return (uint32_t)(r - l);
        }

        uint32_t GetClientHeight() const override
        { // 逻辑
            int l, t, r, b;
            GetClientRect(l, t, r, b);
            return (uint32_t)(b - t);
        }

        void GetClientRectPhysical(int &l, int &t, int &r, int &b) const override
        { // 物理
            if (!m_Hwnd)
            {
                l = t = r = b = 0;
                return;
            }
            RECT rc;
            ::GetClientRect(m_Hwnd, &rc);
            l = rc.left;
            t = rc.top;
            r = rc.right;
            b = rc.bottom;
        }

        uint32_t GetClientWidthPhysical() const override
        { // 物理
            int l, t, r, b;
            GetClientRectPhysical(l, t, r, b);
            return (uint32_t)(r - l);
        }

        uint32_t GetClientHeightPhysical() const override
        { // 物理
            int l, t, r, b;
            GetClientRectPhysical(l, t, r, b);
            return (uint32_t)(b - t);
        }

        void SetClientSize(uint32_t width, uint32_t height) override {} // 逻辑宽高

        void ScreenToClient(int &x, int &y) const override
        { // 输入物理, 输出逻辑
            if (!m_Hwnd)
                return;
            POINT pt = {x, y};
            ::ScreenToClient(m_Hwnd, &pt);
            float s = Dpi::GetScale();
            x = (int)(pt.x / s);
            y = (int)(pt.y / s);
        }

        void ClientToScreen(int &x, int &y) const override
        { // 输入逻辑, 输出物理
            if (!m_Hwnd)
                return;
            float s = Dpi::GetScale();
            POINT pt = {(LONG)(x * s), (LONG)(y * s)};
            ::ClientToScreen(m_Hwnd, &pt);
            x = pt.x;
            y = pt.y;
        }

        void ScreenToClientPhysical(int &x, int &y) const override
        { // 物理 → 物理
            if (!m_Hwnd)
                return;
            POINT pt = {x, y};
            ::ScreenToClient(m_Hwnd, &pt);
            x = pt.x;
            y = pt.y;
        }

        void ClientToScreenPhysical(int &x, int &y) const override
        { // 物理 → 物理
            if (!m_Hwnd)
                return;
            POINT pt = {x, y};
            ::ClientToScreen(m_Hwnd, &pt);
            x = pt.x;
            y = pt.y;
        }

        void CaptureMouse() override
        {
            if (m_Hwnd)
                ::SetCapture(m_Hwnd);
        }
        void ReleaseMouseCapture() override { ::ReleaseCapture(); }

        void SetCursorStyle(CursorStyle style) override
        {
            if (style == CursorStyle::None)
            {
                ::SetCursor(nullptr);
                return;
            }
            static const LPCWSTR ids[] = {
                IDC_ARROW, IDC_SIZEWE, IDC_SIZENS,
                IDC_SIZEALL, IDC_HAND, IDC_IBEAM};
            int idx = static_cast<int>(style);
            HCURSOR hCursor = ::LoadCursor(nullptr,
                                           (idx >= 0 && idx < 6) ? ids[idx] : IDC_ARROW);
            if (hCursor)
                ::SetCursor(hCursor);
        }

        // 运行时可切换置顶/取消置顶
        void SetTopMost(bool topmost) override
        {
            if (!m_Hwnd)
                return;
            ::SetWindowPos(m_Hwnd,
                           topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                           0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }

        // 分层透明属性(机械转发)：具体抠色/alpha 由上层定
        void SetLayeredAttribute(uint32_t colorKey, uint8_t alpha, uint32_t flags) override
        {
            if (!m_Hwnd)
                return;
            ::SetLayeredWindowAttributes(m_Hwnd,
                                         (COLORREF)colorKey, alpha, flags);
        }

        void EnableFileDrop(bool enabled, FileDropCallbacks callbacks) override;
        bool IsFileDragging() const override { return m_IsFileDragging; }

        void MoveAndResize(int x, int y, int w, int h, bool noZOrder) override
        {
            if (!m_Hwnd)
                return;

            const float scale = Dpi::GetScale();
            x = static_cast<int>(x * scale + 0.5f);
            y = static_cast<int>(y * scale + 0.5f);
            w = static_cast<int>(w * scale + 0.5f);
            h = static_cast<int>(h * scale + 0.5f);
            UINT flags = SWP_SHOWWINDOW | (noZOrder ? SWP_NOZORDER : 0);
            ::SetWindowPos(m_Hwnd, nullptr, x, y, w, h, flags);
        }

        void MoveAndResizePhysical(int x, int y, int w, int h,
                                   bool noZOrder) override
        {
            if (!m_Hwnd)
                return;

            UINT flags = SWP_SHOWWINDOW | (noZOrder ? SWP_NOZORDER : 0);
            ::SetWindowPos(m_Hwnd, nullptr, x, y, w, h, flags);
        }

        void RequestRepaint() override
        {
            if (m_Hwnd)
                ::InvalidateRect(m_Hwnd, nullptr, FALSE);
        }

        void ValidateWindow() override
        {
            if (m_Hwnd)
                ::ValidateRect(m_Hwnd, nullptr);
        }

        void PaintDirect(std::function<void(Canvas &)> painter) override
        {
            if (!m_Hwnd)
                return;
            RECT rc;
            ::GetClientRect(m_Hwnd, &rc);

            int w = rc.right - rc.left;
            int hc = rc.bottom - rc.top;
            if (w <= 0 || hc <= 0)
                return;
            // Canvas 构造接收窗口句柄(HWND)，内部 Flush 时动态 GetDC/ReleaseDC
            Canvas canvas(w, hc, (void *)m_Hwnd);
            painter(canvas);
            // 双缓冲：把内存位图一次性上屏
            canvas.Flush();
        }

    private:
        class FileDropTarget;
        HWND m_Hwnd = nullptr;
        FileDropTarget *m_FileDropTarget = nullptr;
        FileDropCallbacks m_FileDropCallbacks;
        bool m_IsFileDragging = false;
        bool m_OleInitialized = false;
    };

    class WindowImplWin32::FileDropTarget final : public IDropTarget
    {
    public:
        explicit FileDropTarget(WindowImplWin32 *owner) : m_Owner(owner) {}

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override
        {
            if (!out)
                return E_POINTER;
            *out = nullptr;
            if (riid == IID_IUnknown || riid == IID_IDropTarget)
            {
                *out = static_cast<IDropTarget *>(this);
                AddRef();
                return S_OK;
            }
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++m_Refs; }
        ULONG STDMETHODCALLTYPE Release() override
        {
            ULONG refs = --m_Refs;
            if (refs == 0)
                delete this;
            return refs;
        }

        HRESULT STDMETHODCALLTYPE DragEnter(IDataObject *data, DWORD, POINTL point,
                                            DWORD *effect) override
        {
            m_Files = ExtractFiles(data);
            if (effect)
                *effect = m_Files.empty() ? DROPEFFECT_NONE : DROPEFFECT_COPY;
            if (m_Owner && !m_Files.empty())
            {
                m_Owner->m_IsFileDragging = true;
                Notify(point, false);
            }
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL point, DWORD *effect) override
        {
            if (effect)
                *effect = m_Files.empty() ? DROPEFFECT_NONE : DROPEFFECT_COPY;
            if (m_Owner && !m_Files.empty())
                Notify(point, true);
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE DragLeave() override
        {
            if (m_Owner && m_Owner->m_IsFileDragging)
            {
                if (m_Owner->m_FileDropCallbacks.onLeave)
                    m_Owner->m_FileDropCallbacks.onLeave();
                m_Owner->m_IsFileDragging = false;
            }
            m_Files.clear();
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE Drop(IDataObject *data, DWORD, POINTL point,
                                       DWORD *effect) override
        {
            std::vector<XPath> files = ExtractFiles(data);
            if (effect)
                *effect = files.empty() ? DROPEFFECT_NONE : DROPEFFECT_COPY;
            if (m_Owner && !files.empty())
            {
                int x = point.x, y = point.y;
                m_Owner->ScreenToClient(x, y);
                if (m_Owner->m_FileDropCallbacks.onDrop)
                    m_Owner->m_FileDropCallbacks.onDrop(files, x, y);
                m_Owner->m_IsFileDragging = false;
            }
            m_Files.clear();
            return S_OK;
        }

    private:
        static std::vector<XPath> ExtractFiles(IDataObject *data)
        {
            std::vector<XPath> files;
            if (!data)
                return files;

            FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            STGMEDIUM medium{};
            if (FAILED(data->GetData(&format, &medium)))
                return files;

            HDROP drop = static_cast<HDROP>(GlobalLock(medium.hGlobal));
            if (drop)
            {
                UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
                for (UINT i = 0; i < count; ++i)
                {
                    UINT length = DragQueryFileW(drop, i, nullptr, 0);
                    std::wstring path(length, L'\0');
                    DragQueryFileW(drop, i, path.data(), length + 1);
                    files.emplace_back(std::filesystem::path(path));
                }
                GlobalUnlock(medium.hGlobal);
            }
            ReleaseStgMedium(&medium);
            return files;
        }

        void Notify(POINTL point, bool over)
        {
            int x = point.x, y = point.y;
            m_Owner->ScreenToClient(x, y);
            if (over)
            {
                if (m_Owner->m_FileDropCallbacks.onOver)
                    m_Owner->m_FileDropCallbacks.onOver(m_Files, x, y);
            }
            else
            {
                if (m_Owner->m_FileDropCallbacks.onEnter)
                    m_Owner->m_FileDropCallbacks.onEnter(m_Files, x, y);
            }
        }

        WindowImplWin32 *m_Owner = nullptr;
        ULONG m_Refs = 1;
        std::vector<XPath> m_Files;
    };

    WindowImplWin32::~WindowImplWin32()
    {
        EnableFileDrop(false, {});
        Destroy();
    }

    void WindowImplWin32::EnableFileDrop(bool enabled, FileDropCallbacks callbacks)
    {
        m_FileDropCallbacks = std::move(callbacks);
        if (!m_Hwnd)
            return;

        if (!enabled)
        {
            if (m_FileDropTarget)
            {
                RevokeDragDrop(m_Hwnd);
                m_FileDropTarget->Release();
                m_FileDropTarget = nullptr;
            }
            m_IsFileDragging = false;
            m_FileDropCallbacks = {};
            if (m_OleInitialized)
            {
                OleUninitialize();
                m_OleInitialized = false;
            }
            return;
        }

        if (!m_OleInitialized)
        {
            HRESULT hr = OleInitialize(nullptr);
            if (FAILED(hr) && hr != S_FALSE)
                return;
            m_OleInitialized = true;
        }

        if (!m_FileDropTarget)
        {
            m_FileDropTarget = new FileDropTarget(this);
            if (FAILED(RegisterDragDrop(m_Hwnd, m_FileDropTarget)))
            {
                m_FileDropTarget->Release();
                m_FileDropTarget = nullptr;
            }
        }
    }

    // ── 静态工具实现 ──────────────────────────────
    void WindowImpl::GetMouseScreenPos(int &x, int &y)
    {
        POINT pt;
        ::GetCursorPos(&pt);
        x = pt.x;
        y = pt.y;
    }

    void WindowImpl::ReleaseGlobalMouseCapture()
    {
        ::ReleaseCapture();
    }

    // ── 平台创建函数 ──────────────────────────────
    WindowImpl *CreateWindowImplWin32()
    {
        return new WindowImplWin32();
    }

} // namespace X_Y
