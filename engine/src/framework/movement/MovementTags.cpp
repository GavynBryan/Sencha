#include <framework/movement/MovementTags.h>

#include <framework/gameplay_tags/GameplayTagRegistry.h>

MovementTags RegisterMovementTags(GameplayTagRegistry& registry)
{
    MovementTags tags;
    tags.Controlled        = registry.RegisterTag("movement.controlled").value_or(GameplayTagId{});
    tags.Grounded          = registry.RegisterTag("movement.grounded").value_or(GameplayTagId{});
    tags.GroundedIdle      = registry.RegisterTag("movement.grounded.idle").value_or(GameplayTagId{});
    tags.GroundedWalking   = registry.RegisterTag("movement.grounded.walking").value_or(GameplayTagId{});
    tags.GroundedSprinting = registry.RegisterTag("movement.grounded.sprinting").value_or(GameplayTagId{});
    tags.Airborne          = registry.RegisterTag("movement.airborne").value_or(GameplayTagId{});
    tags.JumpRequested     = registry.RegisterTag("movement.jump.requested").value_or(GameplayTagId{});
    tags.JumpCooldown      = registry.RegisterTag("movement.jump.cooldown").value_or(GameplayTagId{});
    return tags;
}
