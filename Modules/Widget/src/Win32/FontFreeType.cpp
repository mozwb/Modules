#include "Widget/Font.h"
#include "Win32/FontFreeType.h"
#include "Widget/Dpi.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <vector>
#include <mutex>

#ifdef DrawText
#undef DrawText
#endif

namespace X_Y {

    // FreeType 库句柄全局单例（进程共享；多个 Font 复用，避免重复 Init/Release）
    static FT_Library& GetFTLibrary() {
        static FT_Library s_lib = nullptr;
        static std::once_flag s_flag;
        std::call_once(s_flag, [] {
            FT_Init_FreeType(&s_lib);
        });
        return s_lib;
    }

    // UTF-8 → Unicode 码点序列
    static std::vector<unsigned> Utf8ToCodepoints(const char* s) {
        std::vector<unsigned> out;
        if (!s) return out;
        const unsigned char* p = (const unsigned char*)s;
        while (*p) {
            unsigned cp = 0; int n = 0;
            if      (*p < 0x80)      { cp = *p; n = 1; }
            else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; n = 2; }
            else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; n = 3; }
            else if ((*p & 0xF8) == 0xF0) { cp = *p & 0x07; n = 4; }
            else { cp = *p; n = 1; }
            if (n > 1) {
                for (int i = 1; i < n; ++i) {
                    if (p[i] == 0) break;
                    cp = (cp << 6) | (p[i] & 0x3F);
                }
            }
            out.push_back(cp);
            p += n;
        }
        return out;
    }

    // UTF-16 (Windows wchar_t) → Unicode 码点序列
    static std::vector<unsigned> Utf16ToCodepoints(const wchar_t* s) {
        std::vector<unsigned> out;
        if (!s) return out;
        for (size_t i = 0; s[i]; ) {
            unsigned w0 = (unsigned)s[i++];
            if (w0 >= 0xD800 && w0 <= 0xDBFF && s[i]) {
                unsigned w1 = (unsigned)s[i];
                if (w1 >= 0xDC00 && w1 <= 0xDFFF) {
                    ++i;
                    out.push_back(0x10000 + ((w0 - 0xD800) << 10) + (w1 - 0xDC00));
                    continue;
                }
            }
            out.push_back(w0);
        }
        return out;
    }

    FontFreeType::FontFreeType(const FontDesc& desc)
        : m_Desc(desc), m_Face(nullptr), m_FTError(0),
          m_Scale(Dpi::GetScale())
    {
        if (m_Desc.filePath.empty())
            return;   // 无路径不能用 FreeType；工厂不会分发进来
        m_FTError = FT_New_Face(GetFTLibrary(), m_Desc.filePath.c_str(), 0, &m_Face);
        if (m_FTError)
            return;
        FT_Set_Pixel_Sizes(m_Face, 0, (FT_UInt)(m_Desc.size * m_Scale + 0.5f));
    }

    FontFreeType::~FontFreeType() {
        if (m_Face)
            FT_Done_Face(m_Face);
    }

    const FontDesc& FontFreeType::GetDesc() const { return m_Desc; }

    int FontFreeType::GetHeight() const {
        if (!m_Face || m_FTError) return 0;
        float px = (float)(m_Face->size->metrics.height) / 64.0f;
        return (int)(px / m_Scale + 0.5f);
    }
    int FontFreeType::GetAscent() const {
        if (!m_Face || m_FTError) return 0;
        float px = (float)(m_Face->size->metrics.ascender) / 64.0f;
        return (int)(px / m_Scale + 0.5f);
    }
    int FontFreeType::GetAvgCharWidth() const {
        if (!m_Face || m_FTError) return 0;
        float px = (float)(m_Face->size->metrics.max_advance) / 64.0f;
        return (int)(px / m_Scale + 0.5f);
    }

    void FontFreeType::DrawText(const CanvasTarget& target, int x, int y,
                                const char* text, uint32_t color) const {
        if (!text) return;
        DrawCodepoints(target, x, y, Utf8ToCodepoints(text), color);
    }

    void FontFreeType::DrawText(const CanvasTarget& target, int x, int y,
                                const wchar_t* text, uint32_t color) const {
        if (!text) return;
        DrawCodepoints(target, x, y, Utf16ToCodepoints(text), color);
    }

    void FontFreeType::FillText(const CanvasTarget& target,
        int x, int y, int w, int h, int tx, int ty,
        const char* text, uint32_t textColor, uint32_t bgColor) const {
        if (!text || !*text) return;
        FillRectIntoBuffer(target.pixels, target.width, target.height,
            (int)(x*m_Scale+0.5f),(int)(y*m_Scale+0.5f),
            (int)(w*m_Scale+0.5f),(int)(h*m_Scale+0.5f), bgColor, &target);
        DrawCodepoints(target, tx, ty, Utf8ToCodepoints(text), textColor);
    }

    void FontFreeType::FillText(const CanvasTarget& target,
        int x, int y, int w, int h, int tx, int ty,
        const wchar_t* text, uint32_t textColor, uint32_t bgColor) const {
        if (!text || !*text) return;
        FillRectIntoBuffer(target.pixels, target.width, target.height,
            (int)(x*m_Scale+0.5f),(int)(y*m_Scale+0.5f),
            (int)(w*m_Scale+0.5f),(int)(h*m_Scale+0.5f), bgColor, &target);
        DrawCodepoints(target, tx, ty, Utf16ToCodepoints(text), textColor);
    }

    int FontFreeType::MeasureText(const char* text) const {
        if (!text) return 0;
        return MeasureCodepoints(Utf8ToCodepoints(text));
    }

    int FontFreeType::MeasureText(const wchar_t* text) const {
        if (!text) return 0;
        return MeasureCodepoints(Utf16ToCodepoints(text));
    }

    void FontFreeType::DrawCodepoints(const CanvasTarget& target, int x, int y,
                                      const std::vector<unsigned>& cps,
                                      uint32_t color) const {
        if (!m_Face || m_FTError || !target.pixels) return;
        if (cps.empty()) return;

        uint32_t a0 = (color >> 24) & 0xFF;
        uint32_t r0 = (color >> 16) & 0xFF;
        uint32_t g0 = (color >> 8 ) & 0xFF;
        uint32_t b0 = (color      ) & 0xFF;

        int penX = (int)(x * m_Scale + 0.5f);
        float ascentPx = (float)(m_Face->size->metrics.ascender) / 64.0f;
        int baselineY = (int)(y * m_Scale + 0.5f) + (int)(ascentPx + 0.5f);

        for (unsigned cp : cps) {
            FT_UInt gi = FT_Get_Char_Index(m_Face, cp);
            if (!gi) continue;
            if (FT_Load_Glyph(m_Face, gi, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL))
                continue;
            FT_GlyphSlot slot = m_Face->glyph;
            const FT_Bitmap* bm = &slot->bitmap;
            int gx = penX + slot->bitmap_left;
            int gy = baselineY - slot->bitmap_top;
            BlendBitmap(target.pixels, target.width, target.height,
                        bm, gx, gy, a0, r0, g0, b0, &target);
            penX += (slot->advance.x) >> 6;   // 26.6 fixed → 像素
        }
    }

    void FontFreeType::BlendBitmap(uint32_t* out, int bw, int bh,
                                   const FT_Bitmap* bm, int ox, int oy,
                                   uint32_t a0, uint32_t r0, uint32_t g0, uint32_t b0,
                                   const CanvasTarget* target) {
        if (!bm->buffer) return;
        unsigned char* src = bm->buffer;
        int rows = (int)bm->rows, cols = (int)bm->width, pitch = (int)bm->pitch;
        bool gray = (bm->pixel_mode == FT_PIXEL_MODE_GRAY);
        for (int ry = 0; ry < rows; ++ry) {
            for (int cx = 0; cx < cols; ++cx) {
                unsigned g = gray ? src[ry * pitch + cx]
                                  : (src[ry*pitch + cx*4] ? 255 : 0);
                if (g == 0) continue;
                int X = ox + cx, Y = oy + ry;
                if (X < 0 || X >= bw || Y < 0 || Y >= bh) continue;
                if (target && target->clipEnabled &&
                    (X < target->clipX || X >= target->clipX + target->clipW ||
                     Y < target->clipY || Y >= target->clipY + target->clipH))
                    continue; // 遵守裁剪
                uint32_t* d = &out[(size_t)Y * bw + X];
                uint32_t sa = a0 * g / 255;
                if (sa >= 255) {
                    *d = 0xFF000000u | (r0 << 16) | (g0 << 8) | b0;
                    continue;
                }
                if (sa == 0) continue;
                uint32_t da = (*d >> 24) & 0xFF;
                uint32_t dr = (*d >> 16) & 0xFF;
                uint32_t dg = (*d >> 8 ) & 0xFF;
                uint32_t db = (*d      ) & 0xFF;
                uint32_t oa = sa + da * (255 - sa) / 255;
                uint32_t rr = (sa*r0 + (255-sa)*dr) / 255;
                uint32_t gg = (sa*g0 + (255-sa)*dg) / 255;
                uint32_t bb = (sa*b0 + (255-sa)*db) / 255;
                *d = (oa << 24) | (rr << 16) | (gg << 8) | bb;
            }
        }
    }

    int FontFreeType::MeasureCodepoints(const std::vector<unsigned>& cps) const {
        if (!m_Face || m_FTError || cps.empty()) return 0;
        long wpx = 0;
        for (unsigned cp : cps) {
            FT_UInt gi = FT_Get_Char_Index(m_Face, cp);
            if (!gi) continue;
            if (FT_Load_Glyph(m_Face, gi, FT_LOAD_DEFAULT)) continue;
            wpx += m_Face->glyph->advance.x;
        }
        float px = (float)wpx / 64.0f;
        return (int)(px / m_Scale + 0.5f);
    }

    void FontFreeType::FillRectIntoBuffer(uint32_t* pixels, int bw, int bh,
                                          int x, int y, int w, int h, uint32_t color,
                                          const CanvasTarget* target) {
        if (!pixels) return;
        for (int yy = 0; yy < h; ++yy) {
            for (int xx = 0; xx < w; ++xx) {
                int X = x + xx, Y = y + yy;
                if (X < 0 || X >= bw || Y < 0 || Y >= bh) continue;
                if (target && target->clipEnabled &&
                    (X < target->clipX || X >= target->clipX + target->clipW ||
                     Y < target->clipY || Y >= target->clipY + target->clipH))
                    continue; // 遵守裁剪
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

} // namespace X_Y
