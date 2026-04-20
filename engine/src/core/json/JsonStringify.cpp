#include <core/json/JsonStringify.h>

#include <charconv>
#include <cstdio>
#include <string>

namespace
{
    void AppendIndent(std::string& out, int depth)
    {
        out.append(static_cast<size_t>(depth) * 2u, ' ');
    }

    void AppendEscapedString(std::string& out, const std::string& value)
    {
        out.push_back('"');
        for (unsigned char c : value)
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

    void AppendNumber(std::string& out, double value)
    {
        char buffer[64] = {};
        auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value);
        if (ec == std::errc{})
            out.append(buffer, ptr);
    }

    void StringifyValue(std::string& out, const JsonValue& value, bool pretty, int depth)
    {
        if (value.IsNull())
        {
            out += "null";
        }
        else if (value.IsBool())
        {
            out += value.AsBool() ? "true" : "false";
        }
        else if (value.IsNumber())
        {
            AppendNumber(out, value.AsNumber());
        }
        else if (value.IsString())
        {
            AppendEscapedString(out, value.AsString());
        }
        else if (value.IsArray())
        {
            const auto& array = value.AsArray();
            out.push_back('[');
            if (!array.empty())
            {
                if (pretty) out.push_back('\n');
                for (size_t i = 0; i < array.size(); ++i)
                {
                    if (pretty) AppendIndent(out, depth + 1);
                    StringifyValue(out, array[i], pretty, depth + 1);
                    if (i + 1 < array.size()) out.push_back(',');
                    if (pretty) out.push_back('\n');
                }
                if (pretty) AppendIndent(out, depth);
            }
            out.push_back(']');
        }
        else
        {
            const auto& object = value.AsObject();
            out.push_back('{');
            if (!object.empty())
            {
                if (pretty) out.push_back('\n');
                for (size_t i = 0; i < object.size(); ++i)
                {
                    const auto& [key, child] = object[i];
                    if (pretty) AppendIndent(out, depth + 1);
                    AppendEscapedString(out, key);
                    out.push_back(':');
                    if (pretty) out.push_back(' ');
                    StringifyValue(out, child, pretty, depth + 1);
                    if (i + 1 < object.size()) out.push_back(',');
                    if (pretty) out.push_back('\n');
                }
                if (pretty) AppendIndent(out, depth);
            }
            out.push_back('}');
        }
    }
}

std::string JsonStringify(const JsonValue& value, bool pretty)
{
    std::string out;
    StringifyValue(out, value, pretty, 0);
    return out;
}
