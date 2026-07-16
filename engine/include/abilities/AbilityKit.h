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

class EntityStore;
class ResourceStore;
class EngineSchedule;

// Component storage for the ability kit (tag container, attribute set, ability
// set, active effects). Registered in every registry that may hold these
// components. Idempotent.
void RegisterAbilityComponents(EntityStore& world);

// Session definitions (tag, attribute, effect, and ability registries). Registered
// once, in the global registry's resources, and shared by every zone. Idempotent.
void RegisterAbilityDefinitions(ResourceStore& sessionResources);

// Registry-local runtime state (the activation queue). Registered in every
// registry that drains ability activations. Idempotent.
void RegisterAbilityRuntime(ResourceStore& registryResources);

// Register the ability-kit fixed-tick systems (ability activation, then attribute
// resolve, then effect lifetime). This is the kit's standalone order; the movement
// pipeline interleaves its own systems between these, so call this before
// RegisterMovementSystems.
void RegisterAbilityKitSystems(EngineSchedule& schedule);
