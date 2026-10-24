#pragma once

#include <ecs/ComponentId.h>
#include <ecs/EntityId.h>

#include <span>

class GameplayTagQuery;
class GameplayTagRegistry;
class World;

// Whether `entity` is alive, carries every one of `components`, and -- when
// `tags` is given -- holds gameplay tags it matches. An entity with no tag
// container matches as one holding none; without a registry, a tag query fails.
[[nodiscard]] bool EntityMatches(const World& world,
                                 EntityId entity,
                                 std::span<const ComponentId> components,
                                 const GameplayTagQuery* tags,
                                 const GameplayTagRegistry* registry);
