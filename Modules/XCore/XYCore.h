#pragma once
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <memory>
#include <type_traits>
namespace X_Y
{
    template <typename E>
    constexpr std::string_view GetEnumName(E value)
    {
        // 只支持 enum class / enum
        static_assert(std::is_enum_v<E>, "必须是枚举类型");

        // 编译器内置宏，自动提取枚举名（全平台通用：MSVC/GCC/Clang）
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
            oss << GetEnumName(obj); // 只要类型支持 operator<<，就能直接输出
            return oss.str();
        }
        else
        {
            return "NO_TO_STRING";
        }
    }

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

#define BIT(X) (1 << X)

#define XY_BIND_EVENT_FN(fn) [this](auto &&...args) -> decltype(auto) { return this->fn(std::forward<decltype(args)>(args)...); }

}