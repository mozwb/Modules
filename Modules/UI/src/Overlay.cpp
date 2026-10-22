#include "UI/Component/Overlay.h"

namespace X_Y {

void Overlay::OnPaint(Canvas& canvas) {
    // 组件坐标一律【相对所属容器】(0,0 起画)；位置偏移由宿主压 canvas origin 处理。
    const int w = GetWidth(), h = GetHeight();
    if (w <= 0 || h <= 0) return;

    // 填充半透明背景
    canvas.FillRect(0, 0, w, h, m_Color);

    // 如果边框宽度 > 0，画四边
    if (m_BorderWidth > 0) {
        canvas.FillRect(0, 0, w, m_BorderWidth, m_BorderColor);
        canvas.FillRect(0, h - m_BorderWidth, w, m_BorderWidth, m_BorderColor);
        canvas.FillRect(0, 0, m_BorderWidth, h, m_BorderColor);
        canvas.FillRect(w - m_BorderWidth, 0, m_BorderWidth, h, m_BorderColor);
    }
}

}
