#pragma once

#include <ecs/EntityId.h>

class World;

// Whether a body a query struck is the level: the world's static collision,
// something nothing moves or hurts. Level collision belongs to a per-cell entity
// with a collider and nothing else, so having no entity does not identify it.
[[nodiscard]] bool IsLevelGeometry(const World& world, EntityId entity);
