#include "scene_source/Json5Convert.h"

Json5Value Json5FromJson(const JsonValue& value)
{
    if (value.IsBool())
        return Json5Value(value.AsBool());
    if (value.IsNumber())
        return Json5Value(value.AsNumber());
    if (value.IsString())
        return Json5Value(value.AsString());
    if (value.IsArray())
    {
        Json5Value out = Json5Value::MakeArray();
        out.Elements.reserve(value.AsArray().size());
        for (const JsonValue& element : value.AsArray())
            out.Elements.push_back(Json5FromJson(element));
        return out;
    }
    if (value.IsObject())
    {
        Json5Value out = Json5Value::MakeObject();
        out.Members.reserve(value.AsObject().size());
        for (const auto& [key, member] : value.AsObject())
            out.Members.emplace_back(key, Json5FromJson(member));
        return out;
    }
    return Json5Value{};
}

JsonValue Json5ToJson(const Json5Value& value)
{
    switch (value.K)
    {
    case Json5Value::Kind::Null: return JsonValue{};
    case Json5Value::Kind::Bool: return JsonValue(value.Boolean);
    case Json5Value::Kind::Number: return JsonValue(value.Number);
    case Json5Value::Kind::String: return JsonValue(value.Text);
    case Json5Value::Kind::Array:
    {
        JsonValue::Array out;
        out.reserve(value.Elements.size());
        for (const Json5Value& element : value.Elements)
            out.push_back(Json5ToJson(element));
        return JsonValue(std::move(out));
    }
    case Json5Value::Kind::Object:
    {
        JsonValue::Object out;
        out.reserve(value.Members.size());
        for (const Json5Value::Member& member : value.Members)
            out.emplace_back(member.first, Json5ToJson(member.second));
        return JsonValue(std::move(out));
    }
    }
    return JsonValue{};
}
