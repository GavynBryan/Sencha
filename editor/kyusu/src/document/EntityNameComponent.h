#pragma once

#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <core/text/InlineString.h>

#include <string_view>
#include <tuple>

// The authored display name of an entity. Editor-only, like BrushComponent:
// the hierarchy shows and edits it, the document serializes it, and the level
// cook strips it from the passthrough scene so the runtime schema never has to
// know it. The EditorScene factories stamp a default ("Entity", "Brush 1",
// ...) on everything they mint; only loaded legacy content and projection-
// expanded entities may lack one, and those are labeled by their components.
struct EntityNameComponent
{
    InlineString<64> Value;
};

template <>
struct TypeSchema<EntityNameComponent>
{
    static constexpr std::string_view Name = "name";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('E', 'N', 'A', 'M');

    static auto Fields()
    {
        return std::tuple{
            MakeField("value", &EntityNameComponent::Value),
        };
    }
};
