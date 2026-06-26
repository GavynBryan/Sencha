#include "DocumentSerialization.h"

#include "EditorScene.h"

#include <core/serialization/Archive.h>
#include <world/serialization/ComponentStorageTraits.h>
#include <world/serialization/SceneFieldCodec.h>
#include <world/serialization/SceneFormat.h>

#include <cstdint>

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
// here rather than in the engine.
template <>
struct ComponentStorageTraits<BrushComponent>
{
    static constexpr std::uint32_t BinaryChunkId = MakeFourCC('B', 'R', 'S', 'H');

    static void Register(Registry& registry)
    {
        if (!registry.Components.IsRegistered<BrushComponent>())
            registry.Components.RegisterComponent<BrushComponent>();
    }

    static bool Add(Registry& registry, EntityId entity, BrushComponent component)
    {
        if (registry.Components.HasComponent<BrushComponent>(entity))
            return false;
        registry.Components.AddComponent(entity, component);
        return true;
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
}
