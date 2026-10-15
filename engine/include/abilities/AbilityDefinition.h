#pragma once

#include <abilities/AbilityDeclaration.h>
#include <attributes/AttributeId.h>
#include <authored/AuthoredQueryId.h>
#include <authored/AuthoredValue.h>
#include <authored/VerbBinding.h>
#include <core/metadata/DataSchema.h>
#include <ecs/ComponentId.h>
#include <effects/EffectId.h>
#include <gameplay_tags/GameplayTagId.h>
#include <gameplay_tags/GameplayTagQuery.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// An ability declared into one World: every name resolved to that World's ids, and
// every element in flat tables addressed by index. Keys stay beside the indices for
// traces and diagnostics. Built by ResolveAbilityDeclaration.

inline constexpr std::uint16_t kAbilityNoCondition = 0xFFFF;
inline constexpr std::uint8_t kAbilityEndPhase = 0xFF;
inline constexpr std::uint8_t kAbilityNoLocal = 0xFF;

struct AbilityValue
{
    AbilityValueSource Source = AbilityValueSource::Constant;
    AuthoredValue Constant;
    AbilityPort Port = AbilityPort::Actor;
    std::uint8_t Local = kAbilityNoLocal;
    AbilityHitField Field = AbilityHitField::None;
    // The kind a number is handed on as: what the field it feeds declares.
    DataFieldKind As = DataFieldKind::Float;
};

struct AbilityCondition
{
    std::string Key;
    AbilityConditionKind Kind = AbilityConditionKind::Tags;
    GameplayTagQuery Tags;
    bool HasTags = false;
    AttributeId Attribute;
    AbilityCompare Compare = AbilityCompare::AtLeast;
    AbilityValue Value;
    AbilityValue Subject;
    std::vector<ComponentId> Components;
    bool Held = true;
    AuthoredQueryHandle Query;
    // One per argument the query declares, in its order.
    std::vector<AbilityValue> Arguments;
    std::vector<std::uint16_t> Children;
};

struct AbilityOp
{
    std::string Key;
    AbilityOpKind Kind = AbilityOpKind::Invoke;
    std::uint16_t When = kAbilityNoCondition;
    // Invoke, Lease: the binding, and one value per binding input in its order.
    CompiledVerbBinding Binding;
    std::vector<AbilityValue> Inputs;
    std::uint8_t Targets = kAbilityNoLocal;
    // Query: one value per declared argument.
    AuthoredQueryHandle Query;
    std::vector<AbilityValue> Arguments;
    // Query: the local written. Set: the local set.
    std::uint8_t Local = kAbilityNoLocal;
    AbilityValue From;
    // Commit: a bit per cost group.
    std::uint8_t Groups = 0;
    bool ContinueOnFail = false;
    GameplayTagId Cue;
    std::optional<AbilityValue> At;
    // A cue run by a phase's pulse, which the pulse count carries, rather than a
    // discrete one the cue log does.
    bool Pulsed = false;
};

struct AbilityTransition
{
    std::string Key;
    AbilityTransitionTrigger Trigger = AbilityTransitionTrigger::Immediate;
    std::uint32_t Ticks = 0;
    std::uint16_t When = kAbilityNoCondition;
    std::uint8_t To = kAbilityEndPhase;
};

struct AbilityPhase
{
    std::string Key;
    std::string Name;
    AbilityTimeDomain Domain = AbilityTimeDomain::Actor;
    std::uint32_t ProgressTicks = 0;
    std::vector<std::uint16_t> Enter;
    bool Pulses = false;
    std::uint32_t PulseEvery = 1;
    bool PulseFirstOnEntry = true;
    std::vector<std::uint16_t> PulseOps;
    std::vector<std::uint16_t> Hold;
    std::vector<std::uint16_t> Exit;
    std::vector<GameplayTagId> Present;
    std::vector<std::uint16_t> Continue;
    std::vector<AbilityTransition> Then;
};

struct AbilityCostGroup
{
    std::string Key;
    std::string Name;
    AbilityCostTiming Timing = AbilityCostTiming::Commit;
    AbilityCostRepeat Repeat = AbilityCostRepeat::Once;
    std::vector<EffectId> Effects;
};

struct AbilityCooldown
{
    EffectId Effect;
    AbilityCooldownStart Start = AbilityCooldownStart::OnStart;
    std::uint8_t Group = 0;
};

struct AbilityDefinition
{
    std::string DisplayName;
    // What a grant inherits; still names, resolved per grant by the binding table.
    std::optional<AbilityBindingDecl> InputDefault;
    std::vector<AbilityCondition> Conditions;
    std::vector<std::uint16_t> Activation;
    std::vector<std::uint16_t> Continuation;
    std::vector<std::uint16_t> CancelOn;
    std::vector<GameplayTagId> OwnedTags;
    std::vector<GameplayTagId> CancelTags;
    std::uint8_t Instances = 1;
    std::vector<AbilityLocalDecl> Locals;
    std::uint8_t EntitiesLocal = kAbilityNoLocal;
    std::vector<AbilityOp> Ops;
    std::vector<AbilityPhase> Phases;
    std::vector<AbilityCostGroup> Costs;
    std::optional<AbilityCooldown> Cooldown;
    AbilityPredictionLevel Prediction = AbilityPredictionLevel::Presentation;
    bool CancelOnReload = true;
    // A bit per cost group paid at Start.
    std::uint8_t StartGroups = 0;
};
