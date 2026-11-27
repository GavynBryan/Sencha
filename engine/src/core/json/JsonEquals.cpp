#include <core/json/JsonEquals.h>

#include <core/json/JsonValue.h>

bool JsonEquals(const JsonValue& a, const JsonValue& b)
{
    if (a.IsNull() || b.IsNull())
        return a.IsNull() && b.IsNull();
    if (a.IsBool() || b.IsBool())
        return a.IsBool() && b.IsBool() && a.AsBool() == b.AsBool();
    if (a.IsNumber() || b.IsNumber())
        return a.IsNumber() && b.IsNumber() && a.AsNumber() == b.AsNumber();
    if (a.IsString() || b.IsString())
        return a.IsString() && b.IsString() && a.AsString() == b.AsString();
    if (a.IsArray() || b.IsArray())
    {
        if (!a.IsArray() || !b.IsArray() || a.Size() != b.Size())
            return false;
        for (std::size_t i = 0; i < a.Size(); ++i)
            if (!JsonEquals(a.AsArray()[i], b.AsArray()[i]))
                return false;
        return true;
    }
    if (a.Size() != b.Size())
        return false;
    for (const auto& [key, value] : a.AsObject())
    {
        const JsonValue* other = b.Find(key);
        if (other == nullptr || !JsonEquals(value, *other))
            return false;
    }
    return true;
}
