#pragma once
// ════════════════════════════════════════════════════════════
// X_Y::Math::XMath — 自研数学后端（不依赖 glm，边用边补）
//
// 当前进度：Vec2 起步（够用即可，写 MouseFlight 时按需补充）
// 待补：Vec3 / Vec4 / Mat3 / Mat4 / Quat / 常用函数
// ════════════════════════════════════════════════════════════

#include <cmath>

namespace X_Y::Math::XMath {

    // ── Vec2 ────────────────────────────────────────────────
    struct Vec2 {
        float x = 0.0f;
        float y = 0.0f;

        Vec2() = default;
        Vec2(float x_, float y_) : x(x_), y(y_) {}

        // 分量访问
        float& operator[](int i) { return i == 0 ? x : y; }
        const float& operator[](int i) const { return i == 0 ? x : y; }

        // 算术
        Vec2 operator+(const Vec2& o) const { return { x + o.x, y + o.y }; }
        Vec2 operator-(const Vec2& o) const { return { x - o.x, y - o.y }; }
        Vec2 operator*(float s) const { return { x * s, y * s }; }
        Vec2 operator/(float s) const { return { x / s, y / s }; }

        Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
        Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
        Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
        Vec2& operator/=(float s) { x /= s; y /= s; return *this; }

        bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
        bool operator!=(const Vec2& o) const { return !(*this == o); }
    };

    inline Vec2 operator*(float s, const Vec2& v) { return v * s; }

    // ── 向量函数 ────────────────────────────────────────────
    inline float Dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
    inline float Length(const Vec2& v) { return std::sqrt(Dot(v, v)); }
    inline Vec2 Normalize(const Vec2& v) {
        float len = Length(v);
        return len > 0.0f ? v / len : Vec2();
    }
    inline float Distance(const Vec2& a, const Vec2& b) { return Length(a - b); }

    // 数乘快捷：Vec2 * Vec2 逐分量（可选）
    inline Vec2 Hadamard(const Vec2& a, const Vec2& b) { return { a.x * b.x, a.y * b.y }; }

    // ══ 角度工具 ────────────────────────────────────────────
    // 将角度 from 以不超过 maxDelta 的步长趋向 to（单位：弧度，逆时针为正）。
    // 内部按最近方向转（处理 ±π 绕环），不绕大圈；maxDelta >= 0。
    // 帧率无关性由上层负责：maxDelta 传 角速度(rad/s) × dt。
    inline float RotateToward(float from, float to, float maxDelta) {
        if (maxDelta <= 0.0f)
            return from;
        // 算 to-from 的最近差角（归到 (-π, π]）
        float d = std::fmod(to - from, 6.283185307179586f); // 2π
        if (d > 3.141592653589793f) d -= 6.283185307179586f; // π
        if (d < -3.141592653589793f) d += 6.283185307179586f;
        // 一次最多走 maxDelta；够近就直接到位
        if (std::fabs(d) <= maxDelta)
            return to;
        return from + (d > 0.0f ? maxDelta : -maxDelta);
    }

    // 让向量 a 朝目标向量 b 走最短路径旋转，单次最大转角 maxRad(弧度 e0)。
    // 返回旋转后的新向量（长度保持 a 的模长，不匀速时方向趋同）。
    // maxRad<=0 时原样返回 a；b 为零向量时不旋转返回 a。
    inline Vec2 RotateToward(const Vec2 &a, const Vec2 &b, float maxRad) {
        if (maxRad <= 0.0f)
            return a;
        // 目标方向 b 的当前角；b 为零向量无法定向，保持原状
        float to = std::atan2(b.y, b.x);
        float from = std::atan2(a.y, a.x);
        // 若 a 也是零向量，无处可转，保持原状
        if (from == 0.0f && a.x == 0.0f && a.y == 0.0f)
            return a;
        float dir = RotateToward(from, to, maxRad);
        float len = Length(a);
        return { std::cos(dir) * len, std::sin(dir) * len };
    }

    // 计算从当前方向 forward 到“目标点 target”所需旋转的角（返回 (-π, π]）。
    // origin=发射者当前位置(世界坐标)，target=目标点(世界坐标)，forward=当前朝向(任意向量)。
    // 纯角度计算，不修改任何量；是否偏转/偏转幅度由调用方决定(可配合扫锥判断)。
    // ⚠️ 一定要用真正的位置(origin 世界坐标)算，不能用朝向向量当位置——朝向是局部向量，
    //    与目标的世界坐标差量纲不搭，会把方向算歪。
    inline float AngleToTarget(const Vec2 &origin, const Vec2 &target, const Vec2 &forward) {
        Vec2 diff = target - origin; // 世界坐标下的目标方向(关键:用真实位置算)
        float len = Length(diff);
        if (len <= 0.0f)
            return 0.0f; // 目标就在脚下，无需转向
        float to = std::atan2(diff.y, diff.x);
        float from = std::atan2(forward.y, forward.x);
        float d = std::fmod(to - from, 6.283185307179586f);
        if (d > 3.141592653589793f) d -= 6.283185307179586f;
        if (d < -3.141592653589793f) d += 6.283185307179586f;
        return d;
    }

    // ══ 待补：Vec3 / Vec4 / Mat3 / Mat4 / Quat 等 ══

}
