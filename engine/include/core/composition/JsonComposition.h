#pragma once

#include <core/composition/Composition.h>
#include <core/json/JsonEquals.h>
#include <core/json/JsonValue.h>

#include <algorithm>
#include <string>

template <>
struct CompositionValueTraits<JsonValue>
{
    static bool IsObject(const JsonValue& value) { return value.IsObject(); }
    static bool IsArray(const JsonValue& value) { return value.IsArray(); }
    static bool IsNull(const JsonValue& value) { return value.IsNull(); }
    static bool IsString(const JsonValue& value) { return value.IsString(); }
    static std::string_view AsString(const JsonValue& value) { return value.AsString(); }
    static bool Equal(const JsonValue& a, const JsonValue& b) { return JsonEquals(a, b); }

    static JsonValue MakeObject() { return JsonValue(JsonValue::Object{}); }
    static JsonValue MakeArray() { return JsonValue(JsonValue::Array{}); }
    static JsonValue MakeString(std::string_view text) { return JsonValue(std::string(text)); }

    static JsonValue* FindMember(JsonValue& object, std::string_view key) { return object.Find(key); }
    static const JsonValue* FindMember(const JsonValue& object, std::string_view key) { return object.Find(key); }
    static void AppendMember(JsonValue& object, std::string_view key, JsonValue value)
    {
        object.AsObject().emplace_back(std::string(key), std::move(value));
    }
    static bool EraseMember(JsonValue& object, std::string_view key)
    {
        return std::erase_if(object.AsObject(), [&](const auto& member) { return member.first == key; }) > 0;
    }
    template <typename F>
    static void ForEachMember(const JsonValue& object, F&& visit)
    {
        for (const auto& [key, value] : object.AsObject())
            visit(std::string_view(key), value);
    }

    static std::size_t Size(const JsonValue& array) { return array.AsArray().size(); }
    static const JsonValue& Element(const JsonValue& array, std::size_t index) { return array.AsArray()[index]; }
    static JsonValue& Element(JsonValue& array, std::size_t index) { return array.AsArray()[index]; }
    static void AppendElement(JsonValue& array, JsonValue value) { array.AsArray().push_back(std::move(value)); }
    static void EraseElement(JsonValue& array, std::size_t index)
    {
        array.AsArray().erase(array.AsArray().begin() + static_cast<std::ptrdiff_t>(index));
    }
};
