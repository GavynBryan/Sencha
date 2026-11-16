#pragma once

#include <ecs/EntityId.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>

class Logger;
class World;

// Tags a full GameplayTagContainer refused (a tag new to one already holding
// Capacity), counted per World. A World resource, made by the first refusal.
struct GameplayTagOverflow
{
    std::uint64_t Refused = 0;
    // What ReportGameplayTagOverflow has accounted for.
    std::uint64_t Reported = 0;
    bool Overflowing = false;
    // The first refusal of the run being reported.
    EntityId FirstHolder;
    GameplayTagId FirstTag;
};

void NoteGameplayTagOverflow(World& world, EntityId holder, GameplayTagId tag);

// Once a tick: logs when refusals begin, naming the first, and when a tick
// passes without one, with how many there were between.
void ReportGameplayTagOverflow(World& world, const Logger& log);
