#include "DocumentSerialization.h"

#include "EditorScene.h"

#include <core/serialization/Archive.h>
#include <world/serialization/ComponentStorageTraits.h>
#include <world/serialization/SceneFieldCodec.h>
#include <world/serialization/SceneFormat.h>
#include <zone/WorldPartitionIds.h>

#include <cstdint>
#include <string>

// A BrushId persists as its raw u32; the mesh it points at is serialized
// separately by EditorDocument as a sidecar (03-§5). Must be visible before
// ComponentSerializer<BrushComponent> is instantiated below.
template <>
struct SceneFieldCodec<BrushId>
{
    static bool Save(IWriteArchive& archive, std::string_view key, BrushId value,
                     SceneSerializationContext&)
    {
        archive.Field(key, static_cast<std::uint32_t>(value.Value));
        return archive.Ok();
    }

    static bool Load(IReadArchive& archive, std::string_view key, BrushId& value,
                     SceneSerializationContext&)
    {
        std::uint32_t raw = 0;
        archive.Field(key, raw);
        value = BrushId{ raw };
        return archive.Ok();
    }
};

// Storage traits must be visible before ComponentSerializer<BrushComponent>
// is instantiated below. BrushComponent is editor-only, so its traits live
// here rather than in the engine. World is the concrete storage API;
// Registry remains the editor document adapter.
template <>
struct ComponentStorageTraits<BrushComponent>
{
    static constexpr std::uint32_t BinaryChunkId = MakeFourCC('B', 'R', 'S', 'H');

    static void Register(World& world)
    {
        if (!world.IsRegistered<BrushComponent>())
            world.RegisterComponent<BrushComponent>();
    }

    static void Register(Registry& registry)
    {
        Register(registry.Components);
    }

    static bool Add(World& world, EntityId entity, BrushComponent component)
    {
        if (world.HasComponent<BrushComponent>(entity))
            return false;
        world.AddComponent(entity, component);
        return true;
    }

    static bool Add(Registry& registry, EntityId entity, BrushComponent component)
    {
        return Add(registry.Components, entity, component);
    }
};

template <>
struct ComponentStorageTraits<BakedBrushComponent>
{
    static constexpr std::uint32_t BinaryChunkId = MakeFourCC('B', 'K', 'B', 'R');

    static void Register(World& world)
    {
        if (!world.IsRegistered<BakedBrushComponent>())
            world.RegisterComponent<BakedBrushComponent>();
    }

    static void Register(Registry& registry)
    {
        Register(registry.Components);
    }

    static bool Add(World& world, EntityId entity, BakedBrushComponent component)
    {
        if (world.HasComponent<BakedBrushComponent>(entity))
            return false;
        world.AddComponent(entity, component);
        return true;
    }

    static bool Add(Registry& registry, EntityId entity, BakedBrushComponent component)
    {
        return Add(registry.Components, entity, component);
    }
};

#include <world/serialization/SceneSerializer.h>

void RegisterDocumentSerializers()
{
    static bool registered = false;
    if (registered)
        return;
    registered = true;

    // Engine components: LocalTransform, CameraComponent, StaticMeshComponent.
    InitSceneSerializer();

    // Editor-only components.
    RegisterComponent<BrushComponent>();
    RegisterComponent<BakedBrushComponent>();
}
