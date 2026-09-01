#pragma once

#include <core/metadata/ComponentRemovable.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <ecs/EntityId.h>
#include <ecs/World.h>
#include <world/serialization/ComponentStorageTraits.h>
#include <world/transform/TransformComponents.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// Authoring shape and storage contract for the transform trio.
//
// LocalTransform is the one component whose registration is structural: the
// world transform and the parent link register with it and are seeded from it,
// which is why its ComponentStorageTraits specialization lives here rather than
// in the generic storage header every serializer includes.
//=============================================================================

template <>
struct TypeSchema<LocalTransform>
{
    static constexpr std::string_view Name = "Transform";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('X', 'F', 'R', 'M');
    // Where a thing is, which is the one fact every peer needs about every
    // entity it can see.
    static constexpr bool Replicated = true;
    // A player moves the moment the key goes down rather than a round trip
    // later, so their own machine keeps simulating where they are and treats
    // what arrives as the authority's view to resume from.
    static constexpr bool Predicted = true;

    static auto Fields()
    {
        return std::tuple{
            MakeField("local", &LocalTransform::Value),
        };
    }
};

// Structural: paired with the derived WorldTransform, so the editor must not let
// it be removed (that would orphan the pairing). A transform-less entity is made
// by never adding one, not by stripping it. (core/metadata/ComponentRemovable.h)
template <>
struct ComponentRemovable<LocalTransform>
{
    static constexpr bool Value = false;
};

// LocalTransform is the one structural special case: WorldTransform and Parent
// are not serialized themselves (hierarchy travels separately and
// WorldTransform is derived), so they register alongside LocalTransform, and
// every loaded LocalTransform seeds a matching WorldTransform for propagation.
template <>
struct ComponentStorageTraits<LocalTransform>
{
    static constexpr std::uint32_t BinaryChunkId = TypeSchema<LocalTransform>::SceneChunkId;

    static void Register(World& world)
    {
        if (!world.IsRegistered<LocalTransform>())
            world.RegisterComponent<LocalTransform>();
        if (!world.IsRegistered<WorldTransform>())
            world.RegisterComponent<WorldTransform>();
        if (!world.IsRegistered<Parent>())
            world.RegisterComponent<Parent>();
    }

    static void Register(Registry& registry)
    {
        Register(registry.Components);
    }

    static bool Add(World& world, EntityId entity, LocalTransform component)
    {
        if (world.HasComponent<LocalTransform>(entity))
            return false;

        world.AddComponent(entity, component);
        if (!world.HasComponent<WorldTransform>(entity))
            world.AddComponent(entity, WorldTransform{ component.Value });
        return true;
    }

    static bool Add(Registry& registry, EntityId entity, LocalTransform component)
    {
        return Add(registry.Components, entity, component);
    }
};
