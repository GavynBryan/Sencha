#pragma once

#include <ecs/EntityId.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>

class Logger;
class World;

struct GameplayTagOverflow
{
    std::uint64_t Refused = 0;
    std::uint64_t Reported = 0;
    bool Overflowing = false;
    EntityId FirstHolder;
    GameplayTagId FirstTag;
};

void NoteGameplayTagOverflow(World& world, EntityId holder, GameplayTagId tag);

void ReportGameplayTagOverflow(World& world, const Logger& log);
