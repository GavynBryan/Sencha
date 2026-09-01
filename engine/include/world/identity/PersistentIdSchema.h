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
#include <world/serialization/SceneFieldCodec.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// Authoring shape and index membership for authored entity identity.
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
