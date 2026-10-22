#include "UI/Component/Label.h"
#include "Widget/FontLibrary.h"

namespace X_Y {

    void Label::OnPaint(Canvas& canvas) {
        // 组件坐标一律【相对所属容器】(0,0 起画)；位置偏移由宿主压 canvas origin 处理。
        canvas.FillRect(0, 0, GetWidth(), GetHeight(), m_BgColor);
        auto& font = FontLibrary::Instance().GetDefault();
        canvas.DrawText(font, 0, 0, m_Text.c_str(), m_Color);
    }

}
