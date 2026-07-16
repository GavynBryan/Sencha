#pragma once

#include <ecs/EntityId.h>
#include <effects/EffectId.h>

class World;
class EffectRegistry;
class AttributeRegistry;

//=============================================================================
// Effect runtime (free functions; a game can wrap them in a scheduled system).
//
// Definitions (EffectRegistry, AttributeRegistry for clamping) are passed in by
// the caller, which resolves them once from the session's global resources.
// Required registered components: ActiveEffect, AttributeSet, GameplayTagContainer.
//=============================================================================

// Apply an effect to a target. Instant -> modifies Base once (clamped).
// Duration/Infinite -> spawns an ActiveEffect entity and grants the definition's
// tags to the target's GameplayTagContainer (ref-counted). Performs structural
// changes, so call outside query iteration.
void ApplyEffect(World& world, EntityId target, EffectId effect,
                 const EffectRegistry& effects, const AttributeRegistry& attributes);

// Advance active effects by dt: periodic effects apply their modifiers to Base
// every Period; finite effects count down; expired effects revoke their granted
// tags and are destroyed. Performs structural changes (destroy); call outside
// query iteration.
void TickEffects(World& world, float dt,
                 const EffectRegistry& effects, const AttributeRegistry& attributes);

// Fold active continuous (non-periodic) effect modifiers into AttributeSet
// Current values, in Add -> Multiply -> Override order. Assumes Current has
// already been reset to Base.
void FoldActiveEffects(World& world, const EffectRegistry& effects);

// One-call per-frame attribute resolution with effects:
//   ResetAttributesToBase + FoldActiveEffects + ClampAttributes.
void ResolveAttributesWithEffects(World& world,
                                  const EffectRegistry& effects,
                                  const AttributeRegistry& attributes);
