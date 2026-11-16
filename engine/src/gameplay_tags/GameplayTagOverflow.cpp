#include <gameplay_tags/GameplayTagOverflow.h>

#include <core/logging/Logger.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <utility>

void NoteGameplayTagOverflow(World& world, EntityId holder, GameplayTagId tag)
{
    GameplayTagOverflow& overflow = world.HasResource<GameplayTagOverflow>() ? world.GetResource<GameplayTagOverflow>()
                                                                             : world.AddResource<GameplayTagOverflow>();
    if (overflow.Refused == overflow.Reported && !overflow.Overflowing)
    {
        overflow.FirstHolder = holder;
        overflow.FirstTag = tag;
    }
    ++overflow.Refused;
}

void ReportGameplayTagOverflow(World& world, const Logger& log)
{
    GameplayTagOverflow* overflow = world.TryGetResource<GameplayTagOverflow>();
    if (overflow == nullptr)
        return;
    const std::uint64_t fresh = overflow->Refused - overflow->Reported;
    if (fresh > 0 && !overflow->Overflowing)
    {
        const GameplayTagRegistry* tags = std::as_const(world).TryGetResource<GameplayTagRegistry>();
        log.Warn("gameplay tags: entity {} holds {} tags already and refused '{}'; refusals are counted until "
                 "they stop",
                 overflow->FirstHolder.Index, GameplayTagContainer::Capacity,
                 tags != nullptr ? tags->GetName(overflow->FirstTag) : std::string_view{ "?" });
        overflow->Overflowing = true;
    }
    else if (fresh == 0 && overflow->Overflowing)
    {
        log.Warn("gameplay tags: full containers stopped refusing tags; {} refused in all", overflow->Refused);
        overflow->Overflowing = false;
    }
    overflow->Reported = overflow->Refused;
}
