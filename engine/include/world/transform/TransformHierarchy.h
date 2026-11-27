#pragma once

#include <ecs/EntityId.h>

#include <vector>

class World;

// `root` followed by every entity below it through Parent, depth first. Scans
// every Parent in the World: for a one-off structural move, not for a frame loop.
[[nodiscard]] std::vector<EntityId> RootAndDescendants(const World& world, EntityId root);
