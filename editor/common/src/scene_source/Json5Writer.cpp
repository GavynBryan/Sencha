#include "scene_source/Json5Writer.h"

#include <cctype>
#include <charconv>
#include <cstdio>

namespace
{
    // A scalar array this short renders on one line; anything longer, nested,
    // or commented gets a line per element. 72 keeps a vec4 plus indentation
    // inside conventional line width.
    constexpr std::size_t kInlineArrayLimit = 72;

    void AppendIndent(std::string& out, int depth)
    {
        out.append(static_cast<std::size_t>(depth) * 2u, ' ');
    }

    void AppendComments(std::string& out, std::span<const std::string> comments, int depth)
    {
        for (const std::string& comment : comments)
        {
            AppendIndent(out, depth);
            out += comment;
            out.push_back('\n');
        }
    }

    void AppendNumber(std::string& out, double value)
    {
        char buffer[64] = {};
        const auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value);
        if (ec == std::errc{})
            out.append(buffer, ptr);
    }

    void AppendQuotedString(std::string& out, const std::string& value)
    {
        out.push_back('"');
        for (const unsigned char c : value)
        {
            switch (c)
            {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    char buffer[7] = {};
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                }
                else
                {
                    out.push_back(static_cast<char>(c));
                }
                break;
            }
        }
        out.push_back('"');
    }

    [[nodiscard]] bool IsIdentifierKey(const std::string& key)
    {
        if (key.empty())
            return false;
        if (!(std::isalpha(static_cast<unsigned char>(key.front()))
              || key.front() == '_' || key.front() == '$'))
            return false;
        for (const char c : key)
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$'))
                return false;
        return true;
    }

    void AppendKey(std::string& out, const std::string& key)
    {
        if (IsIdentifierKey(key))
            out += key;
        else
            AppendQuotedString(out, key);
    }

    [[nodiscard]] bool IsScalar(const Json5Value& value)
    {
        return !value.IsArray() && !value.IsObject();
    }

    void AppendScalar(std::string& out, const Json5Value& value)
    {
        switch (value.K)
        {
        case Json5Value::Kind::Null: out += "null"; break;
        case Json5Value::Kind::Bool: out += value.Boolean ? "true" : "false"; break;
        case Json5Value::Kind::Number: AppendNumber(out, value.Number); break;
        case Json5Value::Kind::String: AppendQuotedString(out, value.Text); break;
        default: break;
        }
    }

    // Renders `[a, b, c]` when every element is an uncommented scalar and the
    // result stays short; empty otherwise. Positions and colors read as the
    // tuples they are instead of a column of lonely numbers.
    [[nodiscard]] std::string TryInlineArray(const Json5Value& value)
    {
        if (!value.TrailingComments.empty())
            return {};
        std::string line = "[";
        for (std::size_t i = 0; i < value.Elements.size(); ++i)
        {
            const Json5Value& element = value.Elements[i];
            if (!IsScalar(element) || !element.LeadingComments.empty())
                return {};
            if (i > 0)
                line += ", ";
            AppendScalar(line, element);
            if (line.size() > kInlineArrayLimit)
                return {};
        }
        line.push_back(']');
        return line;
    }

    void AppendValue(std::string& out, const Json5Value& value, int depth)
    {
        if (IsScalar(value))
        {
            AppendScalar(out, value);
            return;
        }

        if (value.IsArray())
        {
            if (value.Elements.empty() && value.TrailingComments.empty())
            {
                out += "[]";
                return;
            }
            if (const std::string inline_ = TryInlineArray(value); !inline_.empty())
            {
                out += inline_;
                return;
            }
            out += "[\n";
            for (const Json5Value& element : value.Elements)
            {
                AppendComments(out, element.LeadingComments, depth + 1);
                AppendIndent(out, depth + 1);
                AppendValue(out, element, depth + 1);
                out += ",\n";
            }
            AppendComments(out, value.TrailingComments, depth + 1);
            AppendIndent(out, depth);
            out.push_back(']');
            return;
        }

        if (value.Members.empty() && value.TrailingComments.empty())
        {
            out += "{}";
            return;
        }
        out += "{\n";
        for (const Json5Value::Member& member : value.Members)
        {
            AppendComments(out, member.second.LeadingComments, depth + 1);
            AppendIndent(out, depth + 1);
            AppendKey(out, member.first);
            out += ": ";
            AppendValue(out, member.second, depth + 1);
            out += ",\n";
        }
        AppendComments(out, value.TrailingComments, depth + 1);
        AppendIndent(out, depth);
        out.push_back('}');
    }
} // namespace

std::string Json5Write(const Json5Value& root, std::span<const std::string> endComments)
{
    std::string out;
    AppendComments(out, root.LeadingComments, 0);
    AppendValue(out, root, 0);
    out.push_back('\n');
    AppendComments(out, endComments, 0);
    return out;
}
