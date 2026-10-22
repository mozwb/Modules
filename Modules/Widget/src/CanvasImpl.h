#pragma once
#include <cstdint>

namespace X_Y {

    // 2D 画布抽象——纯虚接口，隐藏平台绘制实现（软件 ARGB / GDI / 其它后端均可）。
    // 职责：**只做填充图形**（矩形/圆角矩形/三角形/裁剪/清屏/双缓冲上屏）。
    // ⚠️ 文字绘制不在此接口。文字能力归 Font（见 Font.h / FontImpl）。
    // ⚠️ 颜色统一 32bpp ARGB（uint32_t）：0xAARRGGBB。alpha 由各后端自行处理。

    class CanvasImpl {
    public:
        virtual ~CanvasImpl() = default;

        // 逻辑尺寸（DPI 缩放后，供布局/命中测试）
        virtual int GetWidth() const = 0;
        virtual int GetHeight() const = 0;

        // 物理像素尺寸(真实位图/窗口大小)。供整幅清屏/填充需覆盖全部物理像素的场景。
        virtual int GetPhysicalWidth() const = 0;
        virtual int GetPhysicalHeight() const = 0;

        // ── 逻辑 ↔ 物理 互译 ──────────────────────────────
        // 统一坐标体系：上层(布局/物理引擎)一律用逻辑坐标，绘制时后端内部
        // 会自动 ×scale 落到物理像素。需要跟物理像素打交道(鼠标原始坐标/
        // 位图尺寸)时，用 PToL/LToP 换算，不必处处自己乘除 scale。
        // scale 由各后端自持（软件后端 = Dpi::GetScale()，多后端各自独立）。
        virtual float GetScale() const = 0;

        // 逻辑 → 物理像素（与绘制内部 ×scale 同一份换算，保证画在"输入处"）
        virtual int LToP(int logical) const = 0;

        // 物理像素 → 逻辑坐标（鼠标原始坐标/物理位图坐标转入逻辑体系用）
        virtual int PToL(int physical) const = 0;

        // 双缓冲：把内存中已画好的一帧一次性上屏
        virtual void Flush() = 0;

        // 双缓冲局部上屏：只把 [x,y,w,h]（逻辑坐标）这块 BitBlt 上屏。
        // 用于高频局部刷新（如拖动分割线只翻那条窄带），比整窗 Flush 快。
        virtual void FlushRect(int x, int y, int w, int h) = 0;

        // 整幅清屏填色（用物理尺寸填满，带 alpha）。常驻画布每帧开头调用。
        virtual void Clear(uint32_t color) = 0;

        // 填充实心矩形（逻辑坐标）
        virtual void FillRect(int x, int y, int w, int h, uint32_t color) = 0;

        // 将 1/2 通道灰度(+alpha)、RGB 或 RGBA 像素绘制到逻辑坐标矩形。
        // 源像素为自上而下、每通道 8-bit、alpha 为 straight alpha；
        // 目标图像使用最近邻缩放，并遵守当前原点、裁剪区及画布边界。
        virtual void DrawImage(const uint8_t *pixels, uint64_t dataSize,
                               uint32_t sourceWidth, uint32_t sourceHeight,
                               uint32_t channels, int x, int y, int w, int h) = 0;

        // 圆角矩形填充（r=圆角半径像素，逻辑坐标）
        virtual void FillRoundRect(int x, int y, int w, int h,
            int r, uint32_t color) = 0;

        // 填充三角形（三个顶点，逻辑坐标）。用于旋转箭头等自绘形状：
        // 上层先把 3 个点按需旋转/平移，再整体传入。
        virtual void FillTriangle(int x1, int y1, int x2, int y2,
            int x3, int y3, uint32_t color) = 0;

        // 填充圆形/圆环（逻辑坐标）。cx,cy=圆心，rOuter=外半径（逻辑像素，内部×scale到物理），
        // rInner=内半径。rInner<=0 → 实心圆；0<rInner<rOuter → 空心圆环（环宽=rOuter-rInner）。
        // 逐像素判 rInner² ≤ dx²+dy² ≤ rOuter²。
        virtual void FillCircle(int cx, int cy, float rOuter, float rInner, uint32_t color) = 0;

        virtual void SetClip(int x, int y, int w, int h) = 0;
        virtual void ResetClip() = 0;

        // ── 原点平移（坐标系转换）──────────────────────────
        // 语义：把"当前绘制原点"平移 (dx,dy)，之后所有绘制/裁剪/文字的坐标
        // 都相对新原点解释。用于分层 UI 的逐层坐标下钻：
        //   宿主按绝对坐标压入自己的位置 → 子层内部一律按 (0,0) 起画。
        // 可嵌套压栈（每层压自己那一层），PopOrigin 弹回上一层。
        // ⚠️ Clear() 不受 origin 影响（整幅清屏，无坐标语义）；
        //    Flush/FlushRect 是上屏操作，同样不走 origin。
        virtual void PushOrigin(int dx, int dy) = 0;
        virtual void PopOrigin() = 0;

        // 当前原点（逻辑坐标）。Canvas 的文字转发壳需要它把 (x,y) 预先加好，
        // 因为文字绘制走 Font（不认识原点）。
        virtual int GetOriginX() const = 0;
        virtual int GetOriginY() const = 0;

        // 当前裁剪区（物理像素）。Canvas 的文字转发壳把它填进 CanvasTarget，
        // 让 Font 后端也能遵守裁剪（文字绕过 BlitPixel，否则会溢出容器）。
        virtual bool GetClipRect(int &x, int &y, int &w, int &h) const = 0;

        // ── 软件 ARGB 缓冲访问（供 Font 文字绘制拼目标 / 特殊上层直读）──
        // 32bpp ARGB 物理像素指针（布局为 width*height，每像素 0xAARRGGBB）。
        // 后端若不暴露（非软件后端）返回 nullptr。
        virtual uint32_t* GetPixelBuffer() = 0;

        // GDI 桥梁 DC（可选）：软件后端创建的、与像素缓冲共享内存的 memory DC，
        // 供 GDI 文字后端 TextOut 直写（alpha 截断）。非 GDI 后端返回 nullptr。
        virtual void* GetBridgeDC() = 0;
    };

    // ── 文字目标 ──
    // 描述"把字画到哪"：程序集软件 ARGB 缓冲 + 尺寸 + 可选 GDI 桥梁 DC + 裁剪区。
    // Font::DrawText/FillText 用它拼目标。后端自由选择：FreeType 写 pixels；
    // GDI 经 dc (=bridgeDC) 画（alpha 截断）。POD，无逻辑。
    struct CanvasTarget {
        uint32_t* pixels = nullptr;  // ARGB 像素缓冲（物理尺寸）
        int  width  = 0;             // 物理宽
        int  height = 0;             // 物理高
        void* dc = nullptr;          // GDI 桥梁 DC（可选）

        // ── 裁剪区（物理像素）──
        // 文字后端必须遵守：超出此矩形的像素不落。语义与 CanvasImpl::SetClip 一致。
        // clipEnabled=false 时不裁剪（整幅可画）。
        // ⚠️ 之所以要把裁剪传到文字层：文字走 Font 直写像素/桥 DC，绕过了
        //    CanvasImpl 的 BlitPixel 裁剪。不带上它，滚动区文字会溢出到容器外。
        bool clipEnabled = false;
        int  clipX = 0, clipY = 0, clipW = 0, clipH = 0;
    };

    // 平台工厂
    class CanvasFactory {
    public:
        static CanvasImpl* CreateCanvasImpl(int w, int h, void* nativeHandle);
    };

} // namespace X_Y
