#pragma once

#include <ecs/EntityId.h>
#include <abilities/AbilityId.h>

class World;
class AbilityRegistry;
class GameplayTagRegistry;
class EffectRegistry;
class AttributeRegistry;
struct AbilityActivationQueue;

//=============================================================================
// Ability activation (free functions; a game can wrap them in a scheduled system).
//
// Definitions (AbilityRegistry, EffectRegistry, AttributeRegistry, and the
// GameplayTagRegistry) are passed in by the caller, which resolves them once
// from the session's global resources.
// Components used: AbilitySet, GameplayTagContainer, AttributeSet.
//=============================================================================

// Attempt to activate one ability now. Succeeds only if the actor was granted
// the ability, the activation requirements (require/block tags, including any
// cooldown tag) pass against the actor's GameplayTagContainer, and the cost is
// affordable from the actor's AttributeSet (Base). On success applies the Cost,
// Cooldown, and OnActivate effects. Performs structural changes (effect
// entities); call outside query iteration.
bool TryActivateAbility(World& world, EntityId actor, AbilityId ability,
                        const AbilityRegistry& abilities,
                        const GameplayTagRegistry& tags,
                        const EffectRegistry& effects,
                        const AttributeRegistry& attributes);

// Drain the activation queue, attempting each pending intent.
void ProcessAbilityActivations(World& world,
                               AbilityActivationQueue& queue,
                               const AbilityRegistry& abilities,
                               const GameplayTagRegistry& tags,
                               const EffectRegistry& effects,
                               const AttributeRegistry& attributes);
