#pragma once
#include <unordered_map>
#include <string>
#include <vector>
#include <string_view>
#include <stack>

namespace X_Y
{
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
            std::string *stringValue;
            std::vector<JsonValue> *arrayValue;
            std::unordered_map<std::string, JsonValue> *objectValue;
        };

        JsonValue() : type(Type::Null), stringValue(nullptr) {}
        ~JsonValue()
        {
            if (type == Type::String)
                delete stringValue;
            else if (type == Type::Array)
                delete arrayValue;
            else if (type == Type::Object)
                delete objectValue;
        }

        // 先禁用拷贝，防止双重释放
        JsonValue(const JsonValue &) = delete;
        JsonValue &operator=(const JsonValue &) = delete;
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