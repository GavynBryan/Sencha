#pragma once

#include <core/identity/Id.h>
#include <core/json/JsonValue.h>
#include <ecs/EntityId.h>

#include <string>

class ComponentSerializerRegistry;
class EntityBuildPackage;
class World;
struct SceneSerializationContext;

// What a spawned root keeps over the scene it spawns from: the patch from that
// scene's root components to its own, keyed by component, and its persistent id.
struct SceneSpawnOverlay
{
    JsonValue RootPatch = JsonValue(JsonValue::Object{});
    PersistentEntityId RootId;

    [[nodiscard]] bool Empty() const { return RootPatch.Size() == 0 && !RootId.IsValid(); }
};

// The patch from `prefab`'s root to `root`'s components as they are now. Its
// identity, placement and scene instance stay out: a spawn gives those.
[[nodiscard]] SceneSpawnOverlay CaptureSpawnOverlay(const World& world, EntityId root,
                                                    const EntityBuildPackage& prefab,
                                                    const ComponentSerializerRegistry& serializers,
                                                    SceneSerializationContext& context);

// Composes `overlay`'s patch over `package`'s one root. False, with `error`,
// when the package has not exactly one root or names an unknown component.
[[nodiscard]] bool ApplySpawnOverlay(EntityBuildPackage& package, const SceneSpawnOverlay& overlay,
                                     const ComponentSerializerRegistry& serializers, std::string& error);
