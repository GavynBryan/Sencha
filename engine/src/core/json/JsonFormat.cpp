#include <core/json/JsonFormat.h>

#include <charconv>
#include <cmath>
#include <string_view>

namespace
{
    void AppendIndent(std::string& out, int depth, int spaces)
    {
        out.append(static_cast<size_t>(depth * spaces), ' ');
    }

    void AppendEscaped(std::string& out, std::string_view value)
    {
        out.push_back('"');
        for (const unsigned char character : value)
        {
            switch (character)
            {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (character < 0x20)
                {
                    constexpr char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(hex[(character >> 4) & 0x0f]);
                    out.push_back(hex[character & 0x0f]);
                }
                else
                {
                    out.push_back(static_cast<char>(character));
                }
                break;
            }
        }
        out.push_back('"');
    }

    void AppendNumber(std::string& out, double value)
    {
        if (!std::isfinite(value))
        {
            out += "null";
            return;
        }

        char buffer[64]{};
        const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                                std::chars_format::general);
        if (error == std::errc{})
            out.append(buffer, end);
        else
            out += "0";
    }

    void AppendValue(std::string& out,
                     const JsonValue& value,
                     int depth,
                     int spaces)
    {
        if (value.IsNull())
        {
            out += "null";
            return;
        }
        if (value.IsBool())
        {
            out += value.AsBool() ? "true" : "false";
            return;
        }
        if (value.IsNumber())
        {
            AppendNumber(out, value.AsNumber());
            return;
        }
        if (value.IsString())
        {
            AppendEscaped(out, value.AsString());
            return;
        }
        if (value.IsArray())
        {
            const JsonValue::Array& array = value.AsArray();
            if (array.empty())
            {
                out += "[]";
                return;
            }

            out += "[\n";
            for (size_t index = 0; index < array.size(); ++index)
            {
                AppendIndent(out, depth + 1, spaces);
                AppendValue(out, array[index], depth + 1, spaces);
                if (index + 1 < array.size())
                    out.push_back(',');
                out.push_back('\n');
            }
            AppendIndent(out, depth, spaces);
            out.push_back(']');
            return;
        }

        const JsonValue::Object& object = value.AsObject();
        if (object.empty())
        {
            out += "{}";
            return;
        }

        out += "{\n";
        for (size_t index = 0; index < object.size(); ++index)
        {
            AppendIndent(out, depth + 1, spaces);
            AppendEscaped(out, object[index].first);
            out += ": ";
            AppendValue(out, object[index].second, depth + 1, spaces);
            if (index + 1 < object.size())
                out.push_back(',');
            out.push_back('\n');
        }
        AppendIndent(out, depth, spaces);
        out.push_back('}');
    }
}

std::string JsonFormat(const JsonValue& value, int indentSpaces)
{
    if (indentSpaces < 0)
        indentSpaces = 0;

    std::string result;
    AppendValue(result, value, 0, indentSpaces);
    result.push_back('\n');
    return result;
}
