#include "../json.h"
#include <stack>
#include <cctype>
#include "Core/Core.h"

namespace X_Y
{
    bool IsRightKey(const char c)
    {
        return c == '}' || c == ']';
    }
    bool IsLeftKey(const char c)
    {
        return c == '{' || c == '[';
    }
    // 输出和s等长的depth数组
    std::vector<int> BuildDepthMap(const std::string &s)
    {
        std::vector<int> depth(s.size(), 0);
        int cur = 0;
        bool inStr = false;
        bool esc = false;
        for (size_t i = 0; i < s.size(); ++i)
        {
            char c = s[i];
            if (esc)
            {
                esc = false;
                depth[i] = cur;
                continue;
            }
            if (c == '\\')
            {
                esc = true;
                depth[i] = cur;
                continue;
            }
            if (c == '"')
            {
                inStr = !inStr;
                depth[i] = cur;
                continue;
            }
            if (inStr)
            {
                depth[i] = cur;
                continue;
            }

            if (c == '{' || c == '[')
                cur++;
            else if (c == '}' || c == ']')
                cur--;
            depth[i] = cur;
        }
        return depth;
    }

    bool IsRightKey(const char c) { return c == '}' || c == ']'; }
    bool IsLeftKey(const char c) { return c == '{' || c == '['; }

    JsonValue json::Parse(const std::string &JS)
    {
        std::string s = Utils::TrimSpace(JS);
        return ParseValue(s);
    }

    JsonValue json::ParseValue(std::string_view strView)
    {
        std::string s = Utils::TrimSpace(std::string(strView));
        JsonValue v;
        if (s.empty())
        {
            v.type = JsonValue::Type::Null;
            return v;
        }
        if (s.front() == '{')
        {
            return ParseObject(s);
        }
        if (s.front() == '[')
        {
            return ParseArray(s);
        }
        if (s == "true")
        {
            v.type = JsonValue::Type::Boolean;
            v.booleanValue = true;
        }
        else if (s == "false")
        {
            v.type = JsonValue::Type::Boolean;
            v.booleanValue = false;
        }
        else if (s == "null" || s == "none")
        {
            v.type = JsonValue::Type::Null;
        }
        else if (s.front() == '"')
        {
            v.type = JsonValue::Type::String;
            v.stringValue = new std::string(s);
        }
        else
        {
            v.type = JsonValue::Type::Number;
            v.stringValue = new std::string(s);
        }
        return v;
    }

    JsonValue json::ParseObject(std::string_view strView)
    {
        JsonValue Res;
        Res.type = JsonValue::Type::Object;
        Res.objectValue = new JsonObject();
        JsonObject &map = *Res.objectValue;

        std::string src = Utils::TrimSpace(std::string(strView));
        if (src.size() >= 2 && src.front() == '{' && src.back() == '}')
        {
            src = src.substr(1, src.size() - 2);
        }
        std::string s = src;
        auto depth = Utils::BuildDepthMap(s);
        std::vector<size_t> commas;
        for (size_t i = 0; i < s.size(); ++i)
        {
            char c = s[i];
            if (depth[i] != 0)
                continue;
            if (c == ',')
                commas.push_back(i);
        }

        std::vector<std::string> kvPieces;
        size_t last = 0;
        for (size_t pos : commas)
        {
            std::string piece = s.substr(last, pos - pos);
            piece = Utils::TrimSpace(piece);
            if (!piece.empty())
                kvPieces.push_back(piece);
            last = pos + 1;
        }
        std::string lastPiece = s.substr(last);
        lastPiece = Utils::TrimSpace(lastPiece);
        if (!lastPiece.empty())
            kvPieces.push_back(lastPiece);

        for (auto &piece : kvPieces)
        {
            auto pieceDepth = Utils::BuildDepthMap(piece);
            size_t colonPos = std::string::npos;
            for (size_t i = 0; i < piece.size(); ++i)
            {
                if (piece[i] == ':' && pieceDepth[i] == 0)
                {
                    colonPos = i;
                    break;
                }
            }
            if (colonPos == std::string::npos)
                continue;
            std::string keyStr = piece.substr(0, colonPos);
            std::string valRaw = piece.substr(colonPos + 1);
            keyStr = Utils::TrimSpace(keyStr);
            valRaw = Utils::TrimSpace(valRaw);
            if (keyStr.size() >= 2 && keyStr.front() == '"' && keyStr.back() == '"')
            {
                keyStr = keyStr.substr(1, keyStr.size() - 2);
            }
            JsonValue val = ParseValue(valRaw);
            map[keyStr] = std::move(val);
        }
        return Res;
    }

    JsonValue json::ParseArray(std::string_view strView)
    {
        JsonValue res;
        res.type = JsonValue::Type::Array;
        res.arrayValue = new JsonArray();
        JsonArray &arr = *res.arrayValue;

        std::string s = Utils::TrimSpace(std::string(strView));
        if (s.size() >= 2 && s.front() == '[' && s.back() == ']')
        {
            s = s.substr(1, s.size() - 2);
        }
        s = Utils::TrimSpace(s);
        if (s.empty())
            return res;

        auto depth = Utils::BuildDepthMap(s);
        std::vector<size_t> commas;
        for (size_t i = 0; i < s.size(); ++i)
        {
            char c = s[i];
            if (depth[i] != 0)
                continue;
            if (c == ',')
                commas.push_back(i);
        }
        std::vector<std::string> elemPieces;
        size_t last = 0;
        for (size_t pos : commas)
        {
            std::string piece = s.substr(last, pos - last);
            piece = Utils::TrimSpace(piece);
            if (!piece.empty())
                elemPieces.push_back(piece);
            last = pos + 1;
        }
        std::string lastPiece = s.substr(last);
        lastPiece = Utils::TrimSpace(lastPiece);
        if (!lastPiece.empty())
            elemPieces.push_back(lastPiece);

        for (auto &piece : elemPieces)
        {
            arr.push_back(ParseValue(piece));
        }
        return res;
    }
}
