#pragma once

#include <core/identity/Id.h>
#include <core/metadata/ComponentRemovable.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <ecs/World.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/scene/SceneInstance.h>
#include <world/serialization/ComponentStorageTraits.h>
#include <world/serialization/SceneFieldCodec.h>
#include <world/WorldComponentSchemas.h>
#include <world/transform/TransformComponents.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// Authoring shape and identity bookkeeping for the world components.
//
// Registration and the serializers include this. A system that reads one of
// these components includes the component and gets its values, not the
// services that own what the values refer to.
//=============================================================================

// The document owns identity, not the inspector: removing this would strand any
// save-overlay or cross-scene reference joined on the id, and the document would
// then fail to load. EditorScene is the only mutation boundary.
// (core/metadata/ComponentRemovable.h)
template <>
struct ComponentRemovable<PersistentIdComponent>
{
    static constexpr bool Value = false;
};

template <>
struct ComponentTraits<PersistentIdComponent>
{
    static void OnAdd(PersistentIdComponent& component, World& world, EntityId entity)
    {
        if (!component.Id.IsValid())
            return;
        if (auto* index = world.TryGetResource<PersistentEntityIndex>())
            (void)index->Register(component.Id, entity);
    }

    static void OnRemove(const PersistentIdComponent& component, World& world, EntityId entity)
    {
        if (!component.Id.IsValid())
            return;
        if (auto* index = world.TryGetResource<PersistentEntityIndex>())
            index->Unregister(component.Id, entity);
    }
};

template <>
struct SceneFieldCodec<PersistentEntityId>
{
    static bool Save(IWriteArchive&, std::string_view, PersistentEntityId,
                     SceneSerializationContext&);
    static bool Load(IReadArchive&, std::string_view, PersistentEntityId&,
                     SceneSerializationContext&);
};

template <>
struct TypeSchema<PersistentIdComponent>
{
    static constexpr std::string_view Name = "persistent_id";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('P', 'S', 'I', 'D');

    static auto Fields()
    {
        return std::tuple{
            MakeField("id", &PersistentIdComponent::Id),
        };
    }
};

template <>
struct ComponentTraits<SceneInstance>
{
    static void OnAdd(SceneInstance& component, World& world, EntityId entity);
    static void OnRemove(const SceneInstance& component, World& world, EntityId entity);
};

// An asset's stable id as a scene field. Saved as the 16-hex id; load also
// accepts the cook-stamped {"id","path"} ref object -- the id inside the
// stamp is the value, no registry resolution involved -- and reads a bare
// asset:// path (a cook that had no id map) as the invalid id rather than
// failing the entity.
template <>
struct SceneFieldCodec<AssetId>
{
    static bool Save(IWriteArchive&, std::string_view, AssetId,
                     SceneSerializationContext&);
    static bool Load(IReadArchive&, std::string_view, AssetId&,
                     SceneSerializationContext&);
};

template <>
struct TypeSchema<SceneInstance>
{
    static constexpr std::string_view Name = "scene_instance";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('S', 'N', 'I', 'N');

    static auto Fields()
    {
        return std::tuple{
            MakeField("source", &SceneInstance::Source),
            MakeField("id", &SceneInstance::Id),
        };
    }
};

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
