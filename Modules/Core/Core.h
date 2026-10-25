#pragma once

/*
Core.h
version 0.0.0

X_Y 的零依赖基础模块：字符串工具、类型工具、所有权别名。

本模块是整个依赖图的根 —— 它不 include 任何别的 X_Y 模块，
所以任何模块都可以放心依赖它，不会形成环。

────────────────────────────────────────────────────────────────
规则
  . 只放声明和文档；实现全在 src/Core.cpp（模板和 inline 除外）
  . 本头不能 include 任何别的 X_Y 模块
  . 其它模块 include 它请写成 <Core/Core.h>
────────────────────────────────────────────────────────────────
用法

    #include <Core/Core.h>
    using namespace X_Y;

    auto parts = Utils::Split(Utils::Trim(line), ",");
    std::string s = To_Str(someValue);      // 有 toString()/ToString()/operator<< 都行
    auto p = CreateScope<Foo>(1, 2);        // unique_ptr
    auto r = CreateRef<Foo>(1, 2);          // shared_ptr
────────────────────────────────────────────────────────────────
*/

#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace X_Y
{

    // ═══════════════════════════════════════════════════════════════════════
    //  Utils — 字符串工具
    // ═══════════════════════════════════════════════════════════════════════

    namespace Utils
    {
        // ── 判断 ──
        bool starts_with(const std::string &s, const std::string &prefix);
        bool ends_with(const std::string &s, const std::string &suffix);
        bool contains(const std::string &s, const std::string &substr);
        bool equals_ignore_case(const std::string &s1, const std::string &s2);
        bool IsNumber(const std::string &s);
        bool IsBlank(const std::string &s);

        // ── 转换 ──
        std::string ToLower(const std::string &s);
        std::string ToUpper(const std::string &s);

        // ── 裁剪 / 切分 / 替换 ──
        std::string TrimLeft(const std::string &s);
        std::string TrimRight(const std::string &s);
        std::string Trim(const std::string &s);
        std::vector<std::string> Split(const std::string &s, const std::string &delimiter);
        std::string Replace(const std::string &s, const std::string &old_value, const std::string &new_value);

        // ── 解析：整个串必须都是数字才算成功 ──
        bool ParseUnsigned(std::string_view text, std::uint32_t &value);
    } // namespace Utils

    // ═══════════════════════════════════════════════════════════════════════
    //  类型工具
    // ═══════════════════════════════════════════════════════════════════════

    // @@ 取枚举名。
    //    注意顺序：必须定义在 To_Str 之前 —— To_Str 里对它的调用
    //    在模板定义点就要能查到名字。
    //
    //    ⚠ 已知限制（待定）：函数签名里只有类型 E、没有值，所以
    //      __PRETTY_FUNCTION__ 里能截到的是【枚举类型名】（"Color"），
    //      不是枚举值名（"Green"）。所有值返回结果相同。
    template <typename E>
    constexpr std::string_view GetEnumName(E value)
    {
        // 只支持 enum class / enum
        static_assert(std::is_enum_v<E>, "必须是枚举类型");

        // 编译器内置宏，自动提取名字（全平台通用：MSVC/GCC/Clang）
#ifdef _MSC_VER
        std::string_view sig = __FUNCSIG__;
        auto start = sig.find_last_of(' ') + 1;
        auto end = sig.find_last_of(']');
#else
        std::string_view sig = __PRETTY_FUNCTION__;
        auto start = sig.find_first_of('=') + 2;
        auto end = sig.find_last_of(';');
#endif

        sig = sig.substr(start, end - start);
        return sig;
    }

    // ── toString 能力探测 ─────────────────────────────────────────────────────
    // 1. 检测有没有 toString()
    template <typename T, typename = void>
    struct Has_toString : std::false_type
    {
    };

    template <typename T>
    struct Has_toString<T, std::void_t<decltype(std::declval<T>().toString())>>
        : std::true_type
    {
    };

    // 2. 检测有没有 ToString()
    template <typename T, typename = void>
    struct Has_ToString : std::false_type
    {
    };

    template <typename T>
    struct Has_ToString<T, std::void_t<decltype(std::declval<T>().ToString())>>
        : std::true_type
    {
    };

    template <typename T>
    struct HasToString
    {
        // 两个结果
        static constexpr bool has_low = Has_toString<T>::value;
        static constexpr bool has_upp = Has_ToString<T>::value;

        // 或逻辑
        static constexpr bool value = has_low || has_upp;
    };

    template <typename T, typename = void>
    struct HasStatic_toString : std::false_type
    {
    };

    template <typename T>
    struct HasStatic_toString<T, std::void_t<decltype(T::toString())>>
        : std::true_type
    {
    };
    template <typename T, typename = void>
    struct HasStatic_ToString : std::false_type
    {
    };

    template <typename T>
    struct HasStatic_ToString<T, std::void_t<decltype(T::ToString())>>
        : std::true_type
    {
    };
    template <typename T>
    struct HasStaticToString
    {
        // 两个结果
        static constexpr bool has_low = HasStatic_toString<T>::value;
        static constexpr bool has_upp = HasStatic_ToString<T>::value;

        // 或逻辑
        static constexpr bool value = has_low || has_upp;
    };
    template <typename T, typename = void>
    struct IsStreamable : std::false_type
    {
    };
    template <typename T>
    struct IsStreamable<T, std::void_t<decltype(std::declval<std::ostringstream &>() << std::declval<T>())>>
        : std::true_type
    {
    };

    // ── 统一转字符串 ──────────────────────────────────────────────────────────
    // ⚠ 分支顺序有讲究：IsStreamable 在 enum 之前，所以【普通 enum】
    //   会被当成整数打印（"1"），走不到 GetEnumName 分支。
    //   enum class 没有到 int 的隐式转换，才落到枚举分支。
    template <class T>
    inline std::string To_Str(const T &obj)
    {

        if constexpr (std::is_pointer_v<T>)
        {
            if (obj == nullptr)
                return "nullptr";
            if constexpr (std::is_same_v<std::remove_cv_t<std::remove_pointer_t<T>>, char>)
            {
                return std::string(obj);
            }
            return To_Str(*obj); // 解引用，递归调用！
        }

        if constexpr (HasToString<T>::value)
        {
            return obj.toString();
        }
        else if constexpr (HasStaticToString<T>::value)
        {
            return T::toString();
        }
        else if constexpr (IsStreamable<T>::value)
        {
            std::ostringstream oss;
            oss << obj; // 只要类型支持 operator<<，就能直接输出
            return oss.str();
        }
        else if constexpr (std::is_enum_v<T>)
        {
            std::ostringstream oss;
            oss << GetEnumName(obj);
            return oss.str();
        }
        else
        {
            return "NO_TO_STRING";
        }
    }

    // ── 所有权别名 ────────────────────────────────────────────────────────────
    template <typename T>
    using Scope = std::unique_ptr<T>;
    template <typename T, typename... Args>
    constexpr Scope<T> CreateScope(Args &&...args)
    {
        return std::make_unique<T>(std::forward<Args>(args)...);
    }

    template <typename T>
    using Ref = std::shared_ptr<T>;
    template <typename T, typename... Args>
    constexpr Ref<T> CreateRef(Args &&...args)
    {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }

// ── 位操作宏 ─────────────────────────────────────────────────────────────
#define BIT(X) (1 << X)

} // namespace X_Y
