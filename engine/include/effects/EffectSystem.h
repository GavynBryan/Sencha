#pragma once

#include <ecs/EntityId.h>
#include <effects/EffectId.h>

class World;

//=============================================================================
// Effect runtime (free functions; a game can wrap them in a scheduled system).
//
// Required world resources: EffectRegistry, AttributeRegistry (for clamping).
// Required registered components: ActiveEffect, AttributeSet, GameplayTagContainer.
//=============================================================================

// Apply an effect to a target. Instant -> modifies Base once (clamped).
// Duration/Infinite -> spawns an ActiveEffect entity and grants the definition's
// tags to the target's GameplayTagContainer (ref-counted). Performs structural
// changes, so call outside query iteration.
void ApplyEffect(World& world, EntityId target, EffectId effect);

// Advance active effects by dt: periodic effects apply their modifiers to Base
// every Period; finite effects count down; expired effects revoke their granted
// tags and are destroyed. Performs structural changes (destroy); call outside
// query iteration.
void TickEffects(World& world, float dt);

// Fold active continuous (non-periodic) effect modifiers into AttributeSet
// Current values, in Add -> Multiply -> Override order. Assumes Current has
// already been reset to Base.
void FoldActiveEffects(World& world);

// One-call per-frame attribute resolution with effects:
//   ResetAttributesToBase + FoldActiveEffects + ClampAttributes.
void ResolveAttributesWithEffects(World& world);
