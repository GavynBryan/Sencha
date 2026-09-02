#pragma once

//=============================================================================
// AbilityKit
//
// Sencha's data-driven gameplay framework. An entity's capabilities and state
// are described entirely by composable data, interpreted by uniform systems —
// there is no per-entity behavior object:
//
//   tags        symbolic state and categories (hierarchical, ref-counted)
//   attributes  scalar stats with a Base value and a derived, clamped Current
//   effects     instant or timed modifiers that change attributes and grant tags
//   abilities   activatable actions, gated by tags and cost, that apply effects
//
// This header is the public entry point; include it to use the whole framework.
//=============================================================================

#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagQuery.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeResolve.h>
#include <attributes/AttributeSet.h>

#include <effects/ActiveEffect.h>
#include <effects/EffectDefinition.h>
#include <effects/EffectRegistry.h>
#include <effects/EffectSystem.h>

#include <abilities/AbilityActivation.h>
#include <abilities/AbilityDefinition.h>
#include <abilities/AbilityRegistry.h>
#include <abilities/AbilitySet.h>
#include <abilities/AbilitySystem.h>

#include <world/ComponentSet.h>

class ComponentRegistrar;
class World;
class EngineSchedule;

// The kit's component vocabulary: tag container, attribute set, ability set,
// active effects.
using AbilityKitComponents = ComponentSet<
    GameplayTagContainer,
    AttributeSet,
    AbilitySet,
    ActiveEffect>;

void RegisterAbilityKitComponents(ComponentRegistrar& registrar);

// The same components plus the per-World resources they need (the
// tag/attribute/effect/ability registries and the activation queue). Resources
// belong to one World and cannot travel in a sealed vocabulary, which is why
// this is a separate call rather than the registrar's business. Idempotent;
// call once per World. Any feature built on the kit (movement) calls this so
// callers cannot forget a piece.
void RegisterAbilityKit(World& world);

// Register the ability-kit fixed-tick systems (ability activation, then attribute
// resolve, then effect lifetime). This is the kit's standalone order; the movement
// pipeline interleaves its own systems between these, so call this before
// RegisterMovementSystems.
void RegisterAbilityKitSystems(EngineSchedule& schedule);
