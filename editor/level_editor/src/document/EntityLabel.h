#pragma once

#include <ecs/EntityId.h>

#include <string>

class World;

// What an entity is called where a person reads it: its authored name, or its
// handle when it has none.
[[nodiscard]] std::string EntityLabel(const World& world, EntityId entity);
