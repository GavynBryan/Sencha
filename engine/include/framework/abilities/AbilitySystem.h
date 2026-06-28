#pragma once

#include <ecs/EntityId.h>
#include <framework/abilities/AbilityId.h>

class World;

//=============================================================================
// Ability activation (free functions; a game can wrap them in a scheduled system).
//
// Resources used: AbilityRegistry, EffectRegistry, GameplayTagRegistry.
// Components used: AbilitySet, GameplayTagContainer, AttributeSet.
//=============================================================================

// Attempt to activate one ability now. Succeeds only if the actor was granted
// the ability, the activation requirements (require/block tags, including any
// cooldown tag) pass against the actor's GameplayTagContainer, and the cost is
// affordable from the actor's AttributeSet (Base). On success applies the Cost,
// Cooldown, and OnActivate effects. Performs structural changes (effect
// entities); call outside query iteration.
bool TryActivateAbility(World& world, EntityId actor, AbilityId ability);

// Drain the AbilityActivationQueue resource, attempting each pending intent.
void ProcessAbilityActivations(World& world);
