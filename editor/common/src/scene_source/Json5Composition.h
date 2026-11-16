#pragma once

#include "scene_source/Json5Value.h"

#include <core/composition/Composition.h>

#include <algorithm>
#include <string>

template <>
struct CompositionValueTraits<Json5Value>
{
    static bool IsObject(const Json5Value& value) { return value.IsObject(); }
    static bool IsArray(const Json5Value& value) { return value.IsArray(); }
    static bool IsNull(const Json5Value& value) { return value.IsNull(); }
    static bool IsString(const Json5Value& value) { return value.IsString(); }
    static std::string_view AsString(const Json5Value& value) { return value.Text; }
    static bool Equal(const Json5Value& a, const Json5Value& b) { return a.SameValueAs(b); }

    static Json5Value MakeObject() { return Json5Value::MakeObject(); }
    static Json5Value MakeArray() { return Json5Value::MakeArray(); }
    static Json5Value MakeNull() { return Json5Value{}; }
    static Json5Value MakeString(std::string_view text) { return Json5Value(std::string(text)); }

    static Json5Value* FindMember(Json5Value& object, std::string_view key) { return object.FindMutable(key); }
    static const Json5Value* FindMember(const Json5Value& object, std::string_view key) { return object.Find(key); }
    static void AppendMember(Json5Value& object, std::string_view key, Json5Value value)
    {
        object.Members.emplace_back(std::string(key), std::move(value));
    }
    static bool EraseMember(Json5Value& object, std::string_view key)
    {
        return std::erase_if(object.Members, [&](const Json5Value::Member& member) { return member.first == key; }) > 0;
    }
    template <typename F>
    static void ForEachMember(const Json5Value& object, F&& visit)
    {
        for (const Json5Value::Member& member : object.Members)
            visit(std::string_view(member.first), member.second);
    }

    static std::size_t Size(const Json5Value& array) { return array.Elements.size(); }
    static const Json5Value& Element(const Json5Value& array, std::size_t index) { return array.Elements[index]; }
    static Json5Value& Element(Json5Value& array, std::size_t index) { return array.Elements[index]; }
    static void AppendElement(Json5Value& array, Json5Value value) { array.Elements.push_back(std::move(value)); }
    static void EraseElement(Json5Value& array, std::size_t index)
    {
        array.Elements.erase(array.Elements.begin() + static_cast<std::ptrdiff_t>(index));
    }
};
