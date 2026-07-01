#include <movement/MovementTags.h>

#include <gameplay_tags/GameplayTagRegistry.h>

MovementTags RegisterMovementTags(GameplayTagRegistry& registry)
{
    MovementTags tags;
    tags.Controlled    = registry.RegisterTag("movement.controlled").value_or(GameplayTagId{});
    tags.Grounded      = registry.RegisterTag("movement.grounded").value_or(GameplayTagId{});
    tags.Airborne      = registry.RegisterTag("movement.airborne").value_or(GameplayTagId{});
    tags.JumpRequested = registry.RegisterTag("movement.jump.requested").value_or(GameplayTagId{});
    tags.JumpCooldown  = registry.RegisterTag("movement.jump.cooldown").value_or(GameplayTagId{});
    return tags;
}
