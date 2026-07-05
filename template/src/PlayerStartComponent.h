#pragma once

#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>

#include <cstdint>
#include <string_view>
#include <tuple>

// Marks where the player avatar spawns: place it on an entity that carries a
// Transform (world scene content on the world path; any scene works). The
// position is the entity's transform, so the component itself is a zero-size
// tag. Game-defined like SpinComponent: the schema below makes it cook and
// edit without any editor code naming it.
struct PlayerStartComponent
{
};

template <>
struct TypeSchema<PlayerStartComponent>
{
    static constexpr std::string_view Name = "player_start";
    // A unique tag identifying this component's chunk in binary scene data.
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('P', 'S', 'T', 'R');

    static auto Fields()
    {
        return std::tuple{};
    }
};
