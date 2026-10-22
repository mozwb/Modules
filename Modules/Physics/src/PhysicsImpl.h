#pragma once
// ════════════════════════════════════════════════════════════
// X_Y::Physics — 中间层：纯虚接口（物理体 + 碰撞后端）
//
// 三层架构（对齐 Widget 层）：
//   上层  Physics.h          稳定接口（Body 门面，上层只碰这个）
//   中层  本文件              纯虚接口 BodyImpl / CollisionBackend
//   下层  src/Builtin2D/      各后端具体实现（2D / 将来 3D / Box2D…）
//   工厂  PhysicsFactory      按类型/配置创建具体后端
//
// Vec：点/向量数据类型，统一贯穿 上层+虚接口+实现。
//   当前 2D = Vec2；换 3D 只改这里一处别名（2D 场景第三维量 0，上层逻辑不变）。
// ════════════════════════════════════════════════════════════

#include "Math/XMathCore.h"
#include <vector>
#include <array>

namespace X_Y::Physics
{

    namespace MATH = X_Y::Math::XMath;

    // ───────── 点/向量类型（换维度只改这里）─────────
    using Vec = MATH::Vec2;

    // 一个三角形（三个本地坐标顶点）——碰撞检测剖分单元
    struct Tri2D
    {
        std::array<Vec, 3> v;
    };

    // ───────── 物理体纯虚接口（各后端实现）─────────
    class BodyImpl
    {
    public:
        virtual ~BodyImpl() = default;

        // ── 几何（圆 or 凸多边形，实现内自动判定）──
        virtual void SetRadius(float r) = 0;
        virtual void AddPoint(const Vec &p) = 0;
        virtual void ClearGeometry() = 0;
        virtual bool IsCircle() const = 0;
        virtual float GetBoundingRadius() const = 0;
        virtual const std::vector<Vec> &GetLocalPoints() const = 0;
        // 世界坐标顶点（本地 + pos 平移），碰撞算法/绘制用
        virtual std::vector<Vec> GetWorldPoints() const = 0;

        // ── 物理状态 ──
        virtual void SetPos(const Vec &p) = 0;
        virtual Vec GetPos() const = 0;
        virtual void SetVel(const Vec &v) = 0;
        virtual Vec GetVel() const = 0;
        virtual void SetMass(float m) = 0;
        virtual float GetMass() const = 0;
        virtual void SetGravity(bool on) = 0;
        virtual bool HasGravity() const = 0;
        virtual void SetUnit(float u) = 0;
        virtual float GetUnit() const = 0;
        // 朝向（弧度，逆时针为正，本地几何绕 pos 旋转）。绘制/碰撞统一用它。
        virtual void SetRotation(float rad) = 0;
        virtual float GetRotation() const = 0;

        // 三角剖分缓存（本地坐标，碰撞检测用）；上一帧位置（穿透检测）
        virtual const std::vector<Tri2D> &GetTriangles() const = 0;
        virtual Vec GetPrevPos() const = 0;
        // 本地点 → 世界点（旋转 + 平移）。绘制/碰撞统一用它，保证朝向一致。
        virtual Vec LocalToWorld(const Vec &local) const = 0;
        // L2 关键点采样（中心 + 顶点均匀采样，最多 maxSamples 个，世界坐标）
        virtual std::vector<Vec> SampleWorldKeyPoints(int maxSamples) const = 0;

        // ── 物理演化（半隐式欧拉）──
        virtual void Integrate(float dt) = 0;
        virtual void ApplyForce(const Vec &f, float dt) = 0;
        virtual void ApplyGravity(float dt) = 0;
    };

    // ───────── 碰撞后端纯虚接口（算法可换）─────────
    class CollisionBackend
    {
    public:
        virtual ~CollisionBackend() = default;
        // 判断 a、b 物理体当前是否相撞（纯几何，不动状态）
        virtual bool Intersect(const BodyImpl &a, const BodyImpl &b) const = 0;
    };

} // namespace X_Y::Physics
