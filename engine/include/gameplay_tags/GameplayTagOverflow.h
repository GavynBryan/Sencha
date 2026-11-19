#pragma once

#include <ecs/EntityId.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>
#include <span>

class Logger;
class World;
struct GameplayTagContainer;

struct GameplayTagOverflow
{
    std::uint64_t Refused = 0;
    std::uint64_t Reported = 0;
    bool Overflowing = false;
    EntityId FirstHolder;
    GameplayTagId FirstTag;
};

void NoteGameplayTagOverflow(World& world, EntityId holder, GameplayTagId tag);

// Counts a refusal when the tags new to `tags` would not all fit.
[[nodiscard]] bool AdmitGameplayTags(World& world, EntityId holder, const GameplayTagContainer& tags,
                                     std::span<const GameplayTagId> granted);

void ReportGameplayTagOverflow(World& world, const Logger& log);
