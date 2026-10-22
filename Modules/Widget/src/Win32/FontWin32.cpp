#include "Widget/Font.h"
#include "Win32/FontWin32.h"
#include "Widget/Dpi.h"
#include <windows.h>
#include <cstring>

#ifdef DrawText
#undef DrawText
#endif

namespace X_Y {

    // ── 裁剪辅助 ──
    // 文字后端不经过 CanvasImpl::BlitPixel，必须自己遵守 CanvasTarget 的裁剪区，
    // 否则滚动区/小容器里的文字会溢出到容器外（盖住 tab 栏、越过分割线）。
    namespace {

        // GDI 侧：把裁剪矩形挂到 DC 上（TextOutW 自身会遵守，抗锯齿边缘也正确）
        struct DcClipScope {
            HDC dc = nullptr;
            bool applied = false;

            DcClipScope(HDC d, const CanvasTarget& t) : dc(d) {
                if (!dc || !t.clipEnabled || t.clipW <= 0 || t.clipH <= 0)
                    return;
                ::SaveDC(dc);
                ::IntersectClipRect(dc, t.clipX, t.clipY,
                                    t.clipX + t.clipW, t.clipY + t.clipH);
                applied = true;
            }
            ~DcClipScope() {
                if (applied && dc)
                    ::RestoreDC(dc, -1);
            }
            DcClipScope(const DcClipScope&) = delete;
            DcClipScope& operator=(const DcClipScope&) = delete;
        };

        // CPU 侧：像素是否落在裁剪区内（供 ForceAlpha / FillRectIntoBuffer 用）
        inline bool InClip(const CanvasTarget& t, int x, int y) {
            if (!t.clipEnabled)
                return true;
            return x >= t.clipX && x < t.clipX + t.clipW &&
                   y >= t.clipY && y < t.clipY + t.clipH;
        }

    } // namespace

    // 把枚举质量映射到 Win32 LOGFONT.lfQuality

    static BYTE ToGdiQuality(FontDesc::Quality q) {
        switch (q) {
            case FontDesc::NonAntiAliased: return NONANTIALIASED_QUALITY;
            case FontDesc::AntiAliased:    return ANTIALIASED_QUALITY;
            case FontDesc::ClearType:      return CLEARTYPE_QUALITY;
            case FontDesc::Default:
            default:                       return DEFAULT_QUALITY;
        }
    }

    FontWin32::FontWin32(const FontDesc& desc)
        : m_Desc(desc), m_Font(nullptr), m_Scale(Dpi::GetScale())
    {
        if (!m_Desc.filePath.empty()) {
            m_Loaded = ::AddFontResourceExA(
                m_Desc.filePath.c_str(), FR_PRIVATE, 0) > 0;
        }

        LOGFONTW lf = { 0 };
        std::wstring wfamily = m_Desc.filePath.empty()
            ? Utf8ToWide(m_Desc.family) : L"";   // 有文件路径用文件，族名为空
        if (wfamily.empty())
            wfamily = L"Microsoft YaHei UI";
        if (wfamily.size() >= (size_t)LF_FACESIZE)
            wfamily.resize(LF_FACESIZE - 1);
        wcscpy_s(lf.lfFaceName, wfamily.c_str());

        lf.lfHeight = -(int)(m_Desc.size * m_Scale + 0.5f);
        lf.lfWeight = m_Desc.bold ? FW_BOLD : FW_NORMAL;
        lf.lfQuality = ToGdiQuality(m_Desc.quality);
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfOutPrecision = OUT_TT_PRECIS;

        m_Font = ::CreateFontIndirectW(&lf);
        if (!m_Font)
            m_Font = (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
    }

    FontWin32::~FontWin32() {
        if (m_Font && m_Font != ::GetStockObject(DEFAULT_GUI_FONT))
            ::DeleteObject(m_Font);
        if (m_Loaded)
            ::RemoveFontResourceExA(m_Desc.filePath.c_str(), FR_PRIVATE, 0);
    }

    const FontDesc& FontWin32::GetDesc() const { return m_Desc; }

    int FontWin32::GetHeight() const {
        HDC dc = ::GetDC(nullptr);
        HFONT old = (HFONT)::SelectObject(dc, m_Font);
        TEXTMETRICW tm; ::GetTextMetricsW(dc, &tm);
        int h = tm.tmHeight + tm.tmExternalLeading;
        ::SelectObject(dc, old);
        ::ReleaseDC(nullptr, dc);
        return h;
    }
    int FontWin32::GetAscent() const {
        HDC dc = ::GetDC(nullptr);
        HFONT old = (HFONT)::SelectObject(dc, m_Font);
        TEXTMETRICW tm; ::GetTextMetricsW(dc, &tm);
        int a = tm.tmAscent;
        ::SelectObject(dc, old);
        ::ReleaseDC(nullptr, dc);
        return a;
    }
    int FontWin32::GetAvgCharWidth() const {
        HDC dc = ::GetDC(nullptr);
        HFONT old = (HFONT)::SelectObject(dc, m_Font);
        TEXTMETRICW tm; ::GetTextMetricsW(dc, &tm);
        int w = tm.tmAveCharWidth;
        ::SelectObject(dc, old);
        ::ReleaseDC(nullptr, dc);
        return w;
    }

    void FontWin32::DrawText(const CanvasTarget& target, int x, int y,
                             const char* text, uint32_t color) const {
        if (!text) return;
        std::wstring ws = Utf8ToWide(text);
        DrawText(target, x, y, ws.c_str(), color);
    }

    void FontWin32::DrawText(const CanvasTarget& target, int x, int y,
                             const wchar_t* text, uint32_t color) const {
        if (!text || !*text) return;
        HDC dc = (HDC)target.dc;
        if (!dc) return;
        DcClipScope clip(dc, target);   // TextOutW 遵守裁剪
        HFONT old = (HFONT)::SelectObject(dc, m_Font);
        ::SetTextColor(dc, RGB((color>>16)&0xFF,(color>>8)&0xFF,color&0xFF));
        ::SetBkMode(dc, TRANSPARENT);
        int px = (int)(x * m_Scale + 0.5f), py = (int)(y * m_Scale + 0.5f);
        ::TextOutW(dc, px, py, text, (int)wcslen(text));
        ForceAlpha(target.pixels, target.width, target.height, px, py, text, &target);
        ::SelectObject(dc, old);
    }

    void FontWin32::FillText(const CanvasTarget& target,
        int x, int y, int w, int h, int tx, int ty,
        const char* text, uint32_t textColor, uint32_t bgColor) const {
        if (!text) return;
        std::wstring ws = Utf8ToWide(text);
        FillText(target, x, y, w, h, tx, ty, ws.c_str(), textColor, bgColor);
    }

    void FontWin32::FillText(const CanvasTarget& target,
        int x, int y, int w, int h, int tx, int ty,
        const wchar_t* text, uint32_t textColor, uint32_t bgColor) const {
        if (!text || !*text) return;
        FillRectIntoBuffer(target.pixels, target.width, target.height,
            (int)(x*m_Scale+0.5f),(int)(y*m_Scale+0.5f),
            (int)(w*m_Scale+0.5f),(int)(h*m_Scale+0.5f), bgColor, &target);
        HDC dc = (HDC)target.dc;
        if (!dc) return;
        DcClipScope clip(dc, target);   // TextOutW 遵守裁剪
        HFONT old = (HFONT)::SelectObject(dc, m_Font);
        ::SetTextColor(dc, RGB((textColor>>16)&0xFF,(textColor>>8)&0xFF,textColor&0xFF));
        ::SetBkMode(dc, OPAQUE);
        ::SetBkColor(dc, RGB((bgColor>>16)&0xFF,(bgColor>>8)&0xFF,bgColor&0xFF));
        int px = (int)(tx*m_Scale+0.5f), py = (int)(ty*m_Scale+0.5f);
        ::TextOutW(dc, px, py, text, (int)wcslen(text));
        ForceAlpha(target.pixels, target.width, target.height, px, py, text, &target);
        ::SetBkMode(dc, TRANSPARENT);
        ::SelectObject(dc, old);
    }

    int FontWin32::MeasureText(const char* text) const {
        if (!text) return 0;
        std::wstring ws = Utf8ToWide(text);
        return MeasureText(ws.c_str());
    }

    int FontWin32::MeasureText(const wchar_t* text) const {
        if (!text || !*text) return 0;
        HDC dc = ::GetDC(nullptr);
        HFONT old = (HFONT)::SelectObject(dc, m_Font);
        SIZE sz = {0,0};
        ::GetTextExtentPoint32W(dc, text, (int)wcslen(text), &sz);
        ::SelectObject(dc, old);
        ::ReleaseDC(nullptr, dc);
        return (int)(sz.cx / m_Scale + 0.5f);
    }

    void FontWin32::ForceAlpha(uint32_t* pixels, int bw, int bh,
                               int px, int py, const wchar_t* text,
                               const CanvasTarget* target) const {
        if (!pixels) return;
        int w = MeasureTextPhys(text);
        HDC dc = ::GetDC(nullptr);
        HFONT old = (HFONT)::SelectObject(dc, m_Font);
        TEXTMETRICW tm; ::GetTextMetricsW(dc, &tm);
        int h = tm.tmHeight;
        ::SelectObject(dc, old);
        ::ReleaseDC(nullptr, dc);
        for (int yy = 0; yy < h; ++yy) {
            for (int xx = 0; xx < w; ++xx) {
                int X = px + xx, Y = py + yy;
                if (X < 0 || X >= bw || Y < 0 || Y >= bh) continue;
                if (target && !InClip(*target, X, Y)) continue; // 遵守裁剪
                pixels[(size_t)Y * bw + X] |= 0xFF000000;
            }
        }
    }
    int FontWin32::MeasureTextPhys(const wchar_t* text) const {
        HDC dc = ::GetDC(nullptr);
        HFONT old = (HFONT)::SelectObject(dc, m_Font);
        SIZE sz = {0,0};
        ::GetTextExtentPoint32W(dc, text, (int)wcslen(text), &sz);
        ::SelectObject(dc, old);
        ::ReleaseDC(nullptr, dc);
        return (int)sz.cx;
    }

    void FontWin32::FillRectIntoBuffer(uint32_t* pixels, int bw, int bh,
                                       int x, int y, int w, int h, uint32_t color,
                                       const CanvasTarget* target) {
        if (!pixels) return;
        for (int yy = 0; yy < h; ++yy) {
            for (int xx = 0; xx < w; ++xx) {
                int X = x + xx, Y = y + yy;
                if (X < 0 || X >= bw || Y < 0 || Y >= bh) continue;
                if (target && !InClip(*target, X, Y)) continue; // 遵守裁剪
                uint32_t* d = &pixels[(size_t)Y * bw + X];
                uint32_t sa = (color >> 24) & 0xFF;
                if (sa >= 255) { *d = color; continue; }
                if (sa == 0)   { continue; }
                uint32_t da = (*d >> 24) & 0xFF;
                uint32_t sr = ((color>>16)&0xFF)*sa/255;
                uint32_t sg = ((color>>8 )&0xFF)*sa/255;
                uint32_t sb = (color&0xFF)*sa/255;
                uint32_t dr = (*d>>16)&0xFF, dg=(*d>>8)&0xFF, db=*d&0xFF;
                uint32_t oa = sa + da*(255-sa)/255;
                uint32_t r  = (sa*sr + (255-sa)*dr)/255;
                uint32_t g  = (sa*sg + (255-sa)*dg)/255;
                uint32_t b  = (sa*sb + (255-sa)*db)/255;
                *d = (oa<<24)|(r<<16)|(g<<8)|b;
            }
        }
    }

    std::wstring FontWin32::Utf8ToWide(const std::string& s) {
        if (s.empty()) return L"";
        int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring w;
        if (n > 0) { w.resize(n); ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n); }
        return w;
    }

} // namespace X_Y
