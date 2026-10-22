#pragma once
#include "Math/XMath.h"
#include <utility>

namespace X_Y
{
    // ════════════════════════════════════════════════════════════
    // RenderMath — Render 模块的数学门面（旧协议宏已废弃）
    // 想换后端：只改下面 USING_MATH 这一行即可，正文引用全不用动。
    //     USING_MATH = Math::GLM   → glm 后端
    //     USING_MATH = Math::XMath → 自研后端（需实现足够接口）
    // ════════════════════════════════════════════════════════════
    namespace RenderMath = Math::GLM;
}
