#pragma once
#include <unordered_map>
#include <string>
#include <vector>
#include <string_view>
#include <stack>
#include <cassert>
#include <type_traits>
namespace X_Y
{
    namespace detail
    {
        template <typename T>
        constexpr bool always_false = false;
    }
    struct JsonValue
    {

        enum class Type
        {
            Null,
            Boolean,
            Number,
            String,
            Array,
            Object
        };

        Type type;
        union
        {
            bool booleanValue;
            std::string *stringValue; // Number、String 共用这个指针
            std::vector<JsonValue> *arrayValue;
            std::unordered_map<std::string, JsonValue> *objectValue;
        };

        JsonValue() : type(Type::Null) {}

        void Release()
        {
            if (type == Type::String || type == Type::Number)
            {
                delete stringValue;
                stringValue = nullptr;
            }
            else if (type == Type::Array)
            {
                delete arrayValue;
                arrayValue = nullptr;
            }
            else if (type == Type::Object)
            {
                delete objectValue;
                objectValue = nullptr;
            }
            type = Type::Null;
        }

        ~JsonValue()
        {
            Release();
        }

        JsonValue(const JsonValue &) = delete;
        JsonValue &operator=(const JsonValue &) = delete;

        JsonValue(JsonValue &&other) noexcept
        {
            type = other.type;
            switch (type)
            {
            case Type::Boolean:
                booleanValue = other.booleanValue;
                other.booleanValue = false;
                break;
            case Type::Number:
                [[fallthrough]];
            case Type::String:
                stringValue = other.stringValue;
                other.stringValue = nullptr;
                break;
            case Type::Array:
                arrayValue = other.arrayValue;
                other.arrayValue = nullptr;
                break;
            case Type::Object:
                objectValue = other.objectValue;
                other.objectValue = nullptr;
                break;
            case Type::Null:
                break;
            }
            other.type = Type::Null;
        }

        JsonValue &operator=(JsonValue &&other) noexcept
        {
            if (this == &other)
                return *this;
            Release();

            type = other.type;
            switch (type)
            {
            case Type::Boolean:
                booleanValue = other.booleanValue;
                other.booleanValue = false;
                break;
            case Type::Number:
                [[fallthrough]];
            case Type::String:
                stringValue = other.stringValue;
                other.stringValue = nullptr;
                break;
            case Type::Array:
                arrayValue = other.arrayValue;
                other.arrayValue = nullptr;
                break;
            case Type::Object:
                objectValue = other.objectValue;
                other.objectValue = nullptr;
                break;
            case Type::Null:
                break;
            }
            other.type = Type::Null;
            return *this;
        }

        template <class T>
        const T &Get() const
        {
            if constexpr (std::is_same_v<T, bool>)
            {
                assert(type == Type::Boolean);
                return booleanValue;
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                assert(type == Type::String || type == Type::Number);
                return *stringValue;
            }
            else if constexpr (std::is_same_v<T, std::unordered_map<std::string, JsonValue>>)
            {
                assert(type == Type::Object);
                return *objectValue;
            }
            else if constexpr (std::is_same_v<T, std::vector<JsonValue>>)
            {
                assert(type == Type::Array);
                return *arrayValue;
            }
            else
            {
                static_assert(detail::always_false<T>, "unsupported type for JsonValue::Get<T>");
            }
        }

        std::string ToString() const;
    };
    using JsonObject = std::unordered_map<std::string, JsonValue>;
    using JsonArray = std::vector<JsonValue>;
    class json
    {
    public:
        JsonValue Parse(const std::string &JS);
        json() = default;
        ~json() = default;

    private:
        JsonValue ParseValue(std::string_view str);
        JsonValue ParseObject(std::string_view str);
        JsonValue ParseArray(std::string_view str);
    };
}