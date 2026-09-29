#pragma once

#include <anim/AnimRequestSet.h>
#include <anim/AnimTypes.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/EntityId.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>

// The animation an activation asks of its actor. A Held request lasts while the
// activation's effect does; an activation with no lasting effect holds it for its
// one tick. See docs/gameplay/abilitykit.md, "Animation".
struct AbilityAnimation
{
    // None: the ability asks for no animation.
    GameplayTagId Intent;
    AnimRequestLifetime Lifetime = AnimRequestLifetime::Held;
    std::uint32_t FixedTicks = 0;
    std::uint8_t Layers = kAnimAllLayers;
};

// The Held requests an actor's activations own, each until the effect entity that
// is the activation ends. The ability kit cancels a request whose activation has.
struct SENCHA_COMPONENT("sencha.ability_animation_leases") AbilityAnimationLeases
{
    struct Lease
    {
        EntityId Activation;
        AnimRequestId Request;
    };
    Lease Leases[kAnimRequestCapacity] = {};
};

#if !defined(SENCHA_CODEGEN)
#  include <abilities/AbilityAnimation.sencha.h>
#endif
