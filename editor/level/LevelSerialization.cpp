#include "LevelSerialization.h"

#include "LevelScene.h"

#include <world/serialization/ComponentStorageTraits.h>
#include <world/serialization/SceneFormat.h>

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


void RegisterLevelSerializers()
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
