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
//   abilities   phased actions, authored as content, that run operations
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

#include <abilities/AbilityCueLog.h>
#include <abilities/AbilityDefinition.h>
#include <abilities/AbilityGrantEntries.h>
#include <abilities/AbilityGrants.h>
#include <abilities/AbilityLocals.h>
#include <abilities/AbilityRegistry.h>
#include <abilities/AbilityRequestQueue.h>
#include <abilities/AbilitySystem.h>
#include <abilities/AbilityTrace.h>
#include <abilities/ActiveAbilities.h>

#include <world/ComponentSet.h>

class ComponentRegistrar;
class World;
class EngineSchedule;

// The kit's component vocabulary: tags, attributes, effects, and what an actor may
// run and is running.
using AbilityKitComponents = ComponentSet<
    GameplayTagContainer,
    AttributeSet,
    ActiveEffect,
    AbilityGrants,
    ActiveAbilities,
    AbilityLocals,
    AbilityCueLog,
    AbilityPresentationState>;

void RegisterAbilityKitComponents(ComponentRegistrar& registrar);

// The same components plus the per-World resources they need (the vocabulary
// registries, the grant entries, the request queue and the trace). Resources
// belong to one World and cannot travel in a sealed vocabulary, which is why this
// is a separate call rather than the registrar's business. Idempotent; call once
// per World. Any feature built on the kit (movement) calls this so callers cannot
// forget a piece.
void RegisterAbilityKit(World& world);

// The kit's vocabulary registries and nothing else: tags, attributes, effects,
// abilities. What a game's OnRegisterVocabulary declares into, so every host that
// calls that hook installs the same set first. Idempotent, and deliberately not
// RegisterAbilityKit: installing a vocabulary starts nothing that simulates.
void InstallAbilityKitVocabulary(World& world);

// The kit's fixed-tick systems: the actor clock, the ability pass, attribute
// resolve, effect lifetime. The engine registers these for every host before the
// game's hook, with the dispatchers its abilities' operations reach; the movement
// pipeline interleaves its own systems between them.
void RegisterAbilityKitSystems(EngineSchedule& schedule, const AbilityHost& host = {});
