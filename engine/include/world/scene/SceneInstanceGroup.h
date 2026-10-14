#pragma once

#include <ecs/EntityId.h>
#include <world/scene/SceneInstance.h>

#include <span>

class World;

// The one living member without a Parent; invalid when there are none or several.
[[nodiscard]] EntityId SingleRoot(const World& world, std::span<const EntityId> members);

// Destroys every living member SceneInstanceIndex holds for `instance`.
void DestroySceneInstance(World& world, SceneInstanceId instance);
