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

// A TransitionId persists as its 16-hex text form (the manifest's id encoding).
// The all-zeros form is the unlinked portal (invalid id). Malformed text loads
// as unlinked with a logged warning rather than failing: the scene loader
// treats any component-load failure as fatal for the whole scene, and a broken
// portal link must not make a zone unloadable. Validation then reports the
// unlinked portal, so the breakage stays visible.
template <>
struct SceneFieldCodec<TransitionId>
{
    static bool Save(IWriteArchive& archive, std::string_view key, TransitionId value,
                     SceneSerializationContext&)
    {
        const std::string text = TransitionIdToString(value);
        archive.Field(key, std::string_view(text));
        return archive.Ok();
    }

    static bool Load(IReadArchive& archive, std::string_view key, TransitionId& value,
                     SceneSerializationContext& context)
    {
        std::string text;
        archive.Field(key, text);
        if (!archive.Ok())
            return false;
        if (text == std::string(16, '0'))
        {
            value = TransitionId{};
            return true;
        }
        const auto parsed = TransitionIdFromString(text);
        if (!parsed.has_value())
        {
            context.Logging->GetLogger<PortalComponent>().Warn(
                "portal transition id '{}' is malformed; loading as unlinked", text);
            value = TransitionId{};
            return true;
        }
        value = *parsed;
        return true;
    }
};

template <>
struct ComponentStorageTraits<BakedBrushComponent>
{
    static constexpr std::uint32_t BinaryChunkId = MakeFourCC('B', 'K', 'B', 'R');

    static void Register(Registry& registry)
    {
        if (!registry.Components.IsRegistered<BakedBrushComponent>())
            registry.Components.RegisterComponent<BakedBrushComponent>();
    }

    static bool Add(Registry& registry, EntityId entity, BakedBrushComponent component)
    {
        if (registry.Components.HasComponent<BakedBrushComponent>(entity))
            return false;
        registry.Components.AddComponent(entity, component);
        return true;
    }
};

template <>
struct ComponentStorageTraits<PortalComponent>
{
    static constexpr std::uint32_t BinaryChunkId = MakeFourCC('P', 'R', 'T', 'L');

    static void Register(Registry& registry)
    {
        if (!registry.Components.IsRegistered<PortalComponent>())
            registry.Components.RegisterComponent<PortalComponent>();
    }

    static bool Add(Registry& registry, EntityId entity, PortalComponent component)
    {
        if (registry.Components.HasComponent<PortalComponent>(entity))
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
    RegisterComponent<BakedBrushComponent>();
    RegisterComponent<PortalComponent>();
}
