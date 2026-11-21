#include <gameplay_tags/BaselineGameplayTags.h>

#include <ecs/World.h>
#include <gameplay_tags/GameplayTagOverflow.h>

#include <utility>

GameplayTagWrite GrantBaselineTag(World& world, EntityId holder, GameplayTagId tag, std::uint16_t stacks)
{
    if (!world.IsAlive(holder) || !world.IsRegistered<GameplayTagContainer>())
        return GameplayTagWrite::Unchanged;
    if (!world.HasComponent<GameplayTagContainer>(holder))
        world.AddComponent<GameplayTagContainer>(holder, GameplayTagContainer{});
    const GameplayTagWrite written = world.TryGet<GameplayTagContainer>(holder)->GrantBaseline(tag, stacks);
    if (written == GameplayTagWrite::Full)
        NoteGameplayTagOverflow(world, holder, tag);
    return written;
}

bool RevokeBaselineTag(World& world, EntityId holder, GameplayTagId tag, std::uint16_t stacks)
{
    const GameplayTagContainer* held = world.IsAlive(holder) && world.IsRegistered<GameplayTagContainer>()
        ? std::as_const(world).TryGet<GameplayTagContainer>(holder)
        : nullptr;
    if (held == nullptr || stacks == 0 || held->BaselineCount(tag) == 0)
        return false;
    (void)world.TryGet<GameplayTagContainer>(holder)->RevokeBaseline(tag, stacks);
    return true;
}
