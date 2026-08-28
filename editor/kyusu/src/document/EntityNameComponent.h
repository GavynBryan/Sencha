#pragma once

#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <core/text/InlineString.h>

#include <string_view>
#include <tuple>

// The authored display name of an entity. Editor-only, like BrushComponent:
// the hierarchy shows and edits it, the document serializes it, and the level
// cook strips it from the passthrough scene so the runtime schema never has to
// know it. An entity without one is labeled by its components instead, so the
// component exists only on entities somebody actually named.
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
