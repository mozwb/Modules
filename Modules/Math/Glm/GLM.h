#pragma once
// ════════════════════════════════════════════════════════════
// X_Y::Math::GLM — glm 后端命名空间
// 类型用 using 重命名为 PascalCase；函数用 inline 转发封装成 PascalCase。
// 这样 GLM 后端对外暴露统一的 PascalCase 接口，可被 RenderMath 等门面直接使用，
// 未来自研 XMath 后端实现同名函数即可无缝切换。
// ════════════════════════════════════════════════════════════

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/vec1.hpp"
#include "glm/gtc/constants.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/matrix_inverse.hpp"
#include "glm/gtc/type_ptr.hpp"
#include <utility>

namespace X_Y::Math::GLM {

    // ── 类型别名（PascalCase）──
    using Vec1 = ::glm::vec1;
    using Vec2 = ::glm::vec2;
    using Vec3 = ::glm::vec3;
    using Vec4 = ::glm::vec4;
    using IVec2 = ::glm::ivec2;
    using IVec3 = ::glm::ivec3;
    using IVec4 = ::glm::ivec4;
    using Mat2 = ::glm::mat2;
    using Mat3 = ::glm::mat3;
    using Mat4 = ::glm::mat4;
    using Quat = ::glm::quat;
    using DVec3 = ::glm::dvec3;

    // ── 函数（inline 转发封装成 PascalCase，供门面统一调用）──
    inline auto Radians(auto&&... a)     { return ::glm::radians(std::forward<decltype(a)>(a)...); }
    inline auto Degrees(auto&&... a)     { return ::glm::degrees(std::forward<decltype(a)>(a)...); }
    inline auto Dot(auto&&... a)         { return ::glm::dot(std::forward<decltype(a)>(a)...); }
    inline auto Cross(auto&&... a)       { return ::glm::cross(std::forward<decltype(a)>(a)...); }
    inline auto Normalize(auto&&... a)   { return ::glm::normalize(std::forward<decltype(a)>(a)...); }
    inline auto Length(auto&&... a)      { return ::glm::length(std::forward<decltype(a)>(a)...); }
    inline auto Distance(auto&&... a)    { return ::glm::distance(std::forward<decltype(a)>(a)...); }
    inline auto Reflect(auto&&... a)     { return ::glm::reflect(std::forward<decltype(a)>(a)...); }
    inline auto Refract(auto&&... a)     { return ::glm::refract(std::forward<decltype(a)>(a)...); }
    inline auto Translate(auto&&... a)   { return ::glm::translate(std::forward<decltype(a)>(a)...); }
    inline auto Rotate(auto&&... a)      { return ::glm::rotate(std::forward<decltype(a)>(a)...); }
    inline auto Scale(auto&&... a)       { return ::glm::scale(std::forward<decltype(a)>(a)...); }
    inline auto LookAt(auto&&... a)      { return ::glm::lookAt(std::forward<decltype(a)>(a)...); }
    inline auto Perspective(auto&&... a) { return ::glm::perspective(std::forward<decltype(a)>(a)...); }
    inline auto Ortho(auto&&... a)       { return ::glm::ortho(std::forward<decltype(a)>(a)...); }
    inline auto Inverse(auto&&... a)     { return ::glm::inverse(std::forward<decltype(a)>(a)...); }
    inline auto Transpose(auto&&... a)   { return ::glm::transpose(std::forward<decltype(a)>(a)...); }
    inline auto Slerp(auto&&... a)       { return ::glm::slerp(std::forward<decltype(a)>(a)...); }
    inline auto Mix(auto&&... a)         { return ::glm::mix(std::forward<decltype(a)>(a)...); }
    inline auto Clamp(auto&&... a)       { return ::glm::clamp(std::forward<decltype(a)>(a)...); }
    inline auto Value_ptr(auto&&... a)   { return ::glm::value_ptr(std::forward<decltype(a)>(a)...); }
    inline auto ToMat4(auto&&... a)      { return ::glm::mat4_cast(std::forward<decltype(a)>(a)...); }

    // 常量
    inline constexpr float Pi = 3.14159265358979f;

}
