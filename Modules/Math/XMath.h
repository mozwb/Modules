#pragma once

// ════════════════════════════════════════════════════════════
// X_Y::Math — 数学命名空间（多后端，无宏转发）
//
// 两种用法：
//   1) 用 glm 后端：            X_Y::Math::GLM::Vec3
//   2) 用自研后端（XMath）：    X_Y::Math::XMath::Vec2
//
// 模块内换后端：只需改一行
//   using MATH = X_Y::Math::GLM;     // 或
//   using MATH = X_Y::Math::XMath;
// 正文统一写 MATH::Vec2 / MATH::Mat4 等即可。
// ════════════════════════════════════════════════════════════

// ── 后端 1：GLM（第三方库）──
#include "GLM/GLM.h"

// ── 后端 2：XMath（自研，边用边补）──
#include "XMathCore.h"
