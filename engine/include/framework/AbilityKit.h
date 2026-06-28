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

#include <framework/gameplay_tags/GameplayTagContainer.h>
#include <framework/gameplay_tags/GameplayTagQuery.h>
#include <framework/gameplay_tags/GameplayTagRegistry.h>

#include <framework/attributes/AttributeRegistry.h>
#include <framework/attributes/AttributeResolve.h>
#include <framework/attributes/AttributeSet.h>

#include <framework/effects/ActiveEffect.h>
#include <framework/effects/EffectDefinition.h>
#include <framework/effects/EffectRegistry.h>
#include <framework/effects/EffectSystem.h>

#include <framework/abilities/AbilityActivation.h>
#include <framework/abilities/AbilityDefinition.h>
#include <framework/abilities/AbilityRegistry.h>
#include <framework/abilities/AbilitySet.h>
#include <framework/abilities/AbilitySystem.h>
