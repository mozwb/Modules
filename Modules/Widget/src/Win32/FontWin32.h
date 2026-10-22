#pragma once
#include "Widget/Font.h"
#include <windows.h>

#ifdef DrawText
#undef DrawText
#endif

namespace X_Y {

    // ── Font GDI 文字后端 ──────────────────────────────
    // 文字经 CanvasTarget.dc（软件 Canvas 提供的 GDI 桥梁 DC，共享 DIB 像素）TextOut 直写。
    // ⚠️ GDI alpha 截断：GDI 不产生 per-pixel alpha。DrawText/FillText 写字后把文字
    //    包围盒 alpha 置 255(不透明)。真正透明文字走 FreeType 后端(FontFreeType)。
    // ⚠️ 坐标约定：API 收逻辑坐标(x,y)；内部 × DPI scale 得物理像素再 TextOut。
    //    MeasureText 返回逻辑像素宽。
    // 系统字体(无 filePath)走本后端；有 filePath 优先 FreeType。

    class FontWin32 : public FontImpl {
    public:
        explicit FontWin32(const FontDesc& desc);
        ~FontWin32() override;

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
        // target 可空；非空时遵守 target 的裁剪区（文字绕过 BlitPixel，必须自裁）
        void ForceAlpha(uint32_t* pixels, int bw, int bh,
                        int px, int py, const wchar_t* text,
                        const CanvasTarget* target = nullptr) const;
        int  MeasureTextPhys(const wchar_t* text) const;
        static void FillRectIntoBuffer(uint32_t* pixels, int bw, int bh,
                                       int x, int y, int w, int h, uint32_t color,
                                       const CanvasTarget* target = nullptr);
        static std::wstring Utf8ToWide(const std::string& s);

        FontDesc m_Desc;
        HFONT   m_Font = nullptr;
        float   m_Scale = 1.0f;
        bool    m_Loaded = false;
    };

} // namespace X_Y
