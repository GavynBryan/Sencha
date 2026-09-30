#include "DocumentSerialization.h"

#include "scene_source/Json5Convert.h"

#include "EditorScene.h"
#include "EntityNameComponent.h"

#include <core/serialization/Archive.h>
#include <core/serialization/JsonArchive.h>
#include <world/registry/Registry.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/IComponentSerializer.h>
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

#include <world/serialization/SceneSerializer.h>

ComponentSerializerRegistry& EditorSceneSerializers()
{
    static ComponentSerializerRegistry instance;
    return instance;
}

void RegisterDocumentSerializers()
{
    static bool registered = false;
    if (registered)
        return;
    registered = true;

    ComponentSerializerRegistry& serializers = EditorSceneSerializers();

    // Engine components: LocalTransform, CameraComponent, StaticMeshComponent.
    RegisterEngineSceneSerializers(serializers);

    // Editor-only components.
    RegisterComponent<BrushComponent>(serializers);
    RegisterComponent<BakedBrushComponent>(serializers);
    RegisterComponent<EntityNameComponent>(serializers);
}

namespace
{
    std::function<void(World&)>& ModuleVocabulary()
    {
        static std::function<void(World&)> install;
        return install;
    }
}

void SetEditorModuleVocabulary(std::function<void(World&)> install)
{
    ModuleVocabulary() = std::move(install);
}

void InstallEditorModuleVocabulary(World& world)
{
    if (const std::function<void(World&)>& install = ModuleVocabulary(); install)
        install(world);
}

Json5Value SerializeEntityComponents(EntityId entity, const Registry& registry,
                                     SceneSerializationContext& context)
{
    Json5Value components = Json5Value::MakeObject();
    for (const auto& serializer : EditorSceneSerializers().Entries())
    {
        if (serializer->JsonKey() == "persistent_id")
            continue;
        if (!serializer->HasComponent(entity, registry))
            continue;
        JsonWriteArchive archive;
        if (!serializer->Save(archive, entity, registry, context) || !archive.Ok())
            continue;
        JsonValue value = archive.TakeValue();
        if (!value.IsNull())
            components.Members.emplace_back(std::string(serializer->JsonKey()),
                                            Json5FromJson(value));
    }
    return components;
}
