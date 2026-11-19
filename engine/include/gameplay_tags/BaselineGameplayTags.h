#pragma once

#include <ecs/EntityId.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>

class World;

// Gives the holder a container if it has none; a tag that will not fit is refused and counted.
GameplayTagWrite GrantBaselineTag(World& world, EntityId holder, GameplayTagId tag, std::uint16_t stacks = 1);

// Takes at most the stacks the holder keeps; false when it keeps none.
bool RevokeBaselineTag(World& world, EntityId holder, GameplayTagId tag, std::uint16_t stacks);
