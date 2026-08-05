#pragma once

#include <core/json/JsonValue.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

// In-place edits to one authored JSON object, for forms that write named
// members rather than whole subtrees. A form mutates the document's working
// copy directly and reports what it did through FieldEdit, so these stay
// deliberately small: no traversal, no paths, no schema.

[[nodiscard]] inline JsonValue* FindMember(JsonValue& object, std::string_view key)
{
    return object.IsObject() ? object.Find(key) : nullptr;
}

[[nodiscard]] inline const JsonValue* FindMember(const JsonValue& object, std::string_view key)
{
    return object.IsObject() ? object.Find(key) : nullptr;
}

[[nodiscard]] inline std::string ReadMemberString(const JsonValue& object, std::string_view key)
{
    const JsonValue* value = FindMember(object, key);
    return value != nullptr && value->IsString() ? value->AsString() : std::string{};
}

inline void SetMember(JsonValue& object, std::string_view key, JsonValue value)
{
    if (!object.IsObject())
        object = JsonValue(JsonValue::Object{});
    if (JsonValue* existing = object.Find(key))
        *existing = std::move(value);
    else
        object.AsObject().emplace_back(std::string(key), std::move(value));
}

// Removing rather than blanking: an empty member still serializes, and a
// binding that kept the slots of a shape it no longer uses would not compile.
inline void EraseMember(JsonValue& object, std::string_view key)
{
    if (!object.IsObject())
        return;
    auto& members = object.AsObject();
    members.erase(std::remove_if(members.begin(), members.end(),
        [key](const auto& item) { return item.first == key; }), members.end());
}
