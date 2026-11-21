#include <gameplay_tags/GameplayTagOverflow.h>

#include <core/logging/Logger.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
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

bool AdmitGameplayTags(World& world, EntityId holder, const GameplayTagContainer& tags,
                       std::span<const GameplayTagId> granted)
{
    if (tags.CanHoldAll(granted))
        return true;
    const auto refused =
        std::ranges::find_if(granted, [&](GameplayTagId tag) {
            return tag.IsValid() && (!tags.HasExact(tag) || tags.StackCount(tag) == 0xFFFF);
        });
    NoteGameplayTagOverflow(world, holder, refused != granted.end() ? *refused : GameplayTagId{});
    return false;
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
    overflow->QuietReports = fresh > 0 ? 0 : std::min(overflow->QuietReports + 1, kGameplayTagOverflowQuietReports);
    if (overflow->Overflowing && overflow->QuietReports >= kGameplayTagOverflowQuietReports)
    {
        log.Warn("gameplay tags: full containers stopped refusing tags; {} refused in all", overflow->Refused);
        overflow->Overflowing = false;
    }
    overflow->Reported = overflow->Refused;
}
