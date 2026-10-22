#pragma once
#include "Widget/Font.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <vector>

#ifdef DrawText
#undef DrawText
#endif

namespace X_Y {

    // ── Font FreeType 文字后端 ──────────────────────────────
    // 从字体文件(.ttf/.otf)加载，软件光栅化为 per-pixel alpha，直接合成进
    // CanvasTarget 的 32bpp ARGB 缓冲 → 真透明文字（配合 Layered UpdateLayeredWindow）。
    // 相比 GDI：能拿每像素 alpha、抗锯齿好、跨平台。
    // ⚠️ 只用于"有 filePath" 的字体；系统字体(无路径)走 GDI 后端(FontWin32)。
    // ⚠️ 坐标：接口收逻辑坐标，内部 × DPI scale → 物理像素写入缓冲。

    class FontFreeType : public FontImpl {
    public:
        explicit FontFreeType(const FontDesc& desc);
        ~FontFreeType() override;

        const FontDesc& GetDesc() const override;
        int GetHeight() const override;
        int GetAscent() const override;
        int GetAvgCharWidth() const override;

        void DrawText(const CanvasTarget& target, int x, int y,
                      const char* text, uint32_t color) const override;
        void DrawText(const CanvasTarget& target, int x, int y,
                      const wchar_t* text, uint32_t color) const override;
        void FillText(const CanvasTarget& target,
            int x, int y, int w, int h, int tx, int ty,
            const char* text, uint32_t textColor, uint32_t bgColor) const override;
        void FillText(const CanvasTarget& target,
            int x, int y, int w, int h, int tx, int ty,
            const wchar_t* text, uint32_t textColor, uint32_t bgColor) const override;
        int MeasureText(const char* text) const override;
        int MeasureText(const wchar_t* text) const override;

    private:
        void DrawCodepoints(const CanvasTarget& target, int x, int y,
                            const std::vector<unsigned>& cps,
                            uint32_t color) const;
        int  MeasureCodepoints(const std::vector<unsigned>& cps) const;
        // target 可空；非空时遵守其裁剪区（文字绕过 BlitPixel，必须自裁）
        static void BlendBitmap(uint32_t* out, int bw, int bh,
                                const FT_Bitmap* bm, int ox, int oy,
                                uint32_t a0, uint32_t r0, uint32_t g0, uint32_t b0,
                                const CanvasTarget* target);
        static void FillRectIntoBuffer(uint32_t* pixels, int bw, int bh,
                                       int x, int y, int w, int h, uint32_t color,
                                       const CanvasTarget* target = nullptr);

        FontDesc m_Desc;
        FT_Face m_Face = nullptr;
        long    m_FTError = 0;
        float   m_Scale = 1.0f;
    };

} // namespace X_Y
