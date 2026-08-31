#pragma once

// Shared assertions-side readers for cooked .smap output: load a cooked scene
// through the editor's serializer set, and address component payloads by
// their JSON key. Every level_cook suite that inspects cook output goes
// through these instead of hand-rolling the lookup.

#include "document/DocumentSerialization.h"

#include <world/scene/SmapFormat.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string_view>

// The cooked scene at `path`, read with the editor's serializers. A failed
// read is reported and returns empty contents, so callers assert on shape.
[[nodiscard]] inline SmapContents ReadCookedScene(const std::filesystem::path& path)
{
    SmapContents contents;
    SmapError error;
    EXPECT_TRUE(ReadSmapFile(path, EditorSceneSerializers(), contents, &error))
        << error.Message;
    return contents;
}

// One record's payload for the component named by its JSON key, or null.
[[nodiscard]] inline const JsonValue* FindCookedComponent(
    const SmapEntityRecord& record, std::string_view jsonKey)
{
    const IComponentSerializer* serializer =
        EditorSceneSerializers().FindByJsonKey(jsonKey);
    if (serializer == nullptr)
        return nullptr;
    for (const auto& [type, payload] : record.Components)
        if (type == serializer->TypeId())
            return &payload;
    return nullptr;
}

// The first payload of `jsonKey` anywhere in the scene, or nullopt when no
// entity carries one.
[[nodiscard]] inline std::optional<JsonValue> FindFirstCookedComponent(
    const SmapContents& contents, std::string_view jsonKey)
{
    for (const SmapEntityRecord& record : contents.Entities)
        if (const JsonValue* payload = FindCookedComponent(record, jsonKey))
            return *payload;
    return std::nullopt;
}
