#pragma once

#include <authored/VerbBinding.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// An ability as authored: names and keys, checked for shape and nothing a World
// knows. Declaring it into a World resolves the names (AbilityDefinitionResolve.h).
// The format is docs/gameplay/abilitykit.md, "Ability definitions".

enum class AbilityDiagnosticSeverity : std::uint8_t
{
    Error,
    Warning,
    Info,
};

struct AbilityDiagnostic
{
    AbilityDiagnosticSeverity Severity = AbilityDiagnosticSeverity::Error;
    std::string Code;
    // The stable key of the element at fault, when it has one.
    std::string ElementKey;
    std::string Path;
    std::string Message;
};

enum class AbilityPort : std::uint8_t
{
    Actor,
    Instigator,
    AimOrigin,
    AimDirection,
    PhaseProgress,
    PhaseTicks,
    Pulse,
    TimeScale,
    // The current element of an op's `targets`.
    Target,
};

enum class AbilityLocalType : std::uint8_t
{
    Bool,
    Int,
    Float,
    Vec3,
    Entity,
    Hit,
    Entities,
};

enum class AbilityHitField : std::uint8_t
{
    None,
    Entity,
    Point,
    Normal,
};

enum class AbilityValueSource : std::uint8_t
{
    // A constant, tag, asset, data asset or entity, as a verb binding states one.
    Constant,
    Port,
    Local,
};

struct AbilityValueDecl
{
    AbilityValueSource Source = AbilityValueSource::Constant;
    VerbBindingArgument Constant;
    AbilityPort Port = AbilityPort::Actor;
    std::string Local;
    AbilityHitField Field = AbilityHitField::None;
};

struct AbilityArgumentDecl
{
    std::string Name;
    AbilityValueDecl Value;
};

struct AbilityTagQueryDecl
{
    std::vector<std::string> All;
    std::vector<std::string> Any;
    std::vector<std::string> None;

    [[nodiscard]] bool Empty() const { return All.empty() && Any.empty() && None.empty(); }
    friend bool operator==(const AbilityTagQueryDecl&, const AbilityTagQueryDecl&) = default;
};

enum class AbilityCompare : std::uint8_t
{
    AtLeast,
    AtMost,
    Above,
    Below,
    Equals,
    NotEquals,
};

enum class AbilityConditionKind : std::uint8_t
{
    Tags,
    Attribute,
    Local,
    Hit,
    Entity,
    PhaseTicks,
    Pulse,
    Input,
    Query,
    All,
    Any,
    Not,
};

struct AbilityConditionDecl
{
    std::string Key;
    std::string Path;
    AbilityConditionKind Kind = AbilityConditionKind::Tags;
    // Tags; an Entity condition's tags.
    AbilityTagQueryDecl Tags;
    // Attribute: the attribute. Query: the query.
    std::string Name;
    // Attribute, Local, PhaseTicks, Pulse.
    AbilityCompare Compare = AbilityCompare::AtLeast;
    AbilityValueDecl Value;
    // Local, Hit: the local. Entity: the entity.
    AbilityValueDecl Subject;
    // Entity: component identities it must carry.
    std::vector<std::string> Components;
    // Input: held, or not held.
    bool Held = true;
    std::vector<AbilityArgumentDecl> Arguments;
    // All, Any, Not.
    std::vector<AbilityConditionDecl> Children;
};

enum class AbilityOpKind : std::uint8_t
{
    Invoke,
    Query,
    Lease,
    Set,
    Commit,
    Cue,
};

struct AbilityOpDecl
{
    std::string Key;
    std::string Path;
    AbilityOpKind Kind = AbilityOpKind::Invoke;
    std::optional<AbilityConditionDecl> When;
    // Invoke, Query, Lease: the operation's name and arguments.
    std::string Operation;
    std::vector<AbilityArgumentDecl> Arguments;
    // Invoke, Lease: an entities local the op repeats over, as the `target` port.
    std::string Targets;
    // Query: the local it writes. Set: the local it sets.
    std::string Local;
    AbilityValueDecl From;
    std::vector<std::string> Groups;
    bool ContinueOnFail = false;
    std::string Cue;
    std::optional<AbilityValueDecl> At;
};

enum class AbilityTimeDomain : std::uint8_t
{
    Actor,
    Simulation,
};

enum class AbilityTransitionTrigger : std::uint8_t
{
    // Evaluated right after the phase's op lists, in the same step.
    Immediate,
    Release,
    Timeout,
};

struct AbilityTransitionDecl
{
    std::string Key;
    std::string Path;
    AbilityTransitionTrigger Trigger = AbilityTransitionTrigger::Immediate;
    std::uint32_t Ticks = 0;
    std::optional<AbilityConditionDecl> When;
    // A phase key; empty for the end of the activation.
    std::string To;
};

struct AbilityPulseDecl
{
    std::uint32_t Every = 1;
    bool FirstOnEntry = true;
    std::vector<AbilityOpDecl> Ops;
};

struct AbilityPresentDecl
{
    std::string Key;
    std::string Cue;
};

struct AbilityPhaseDecl
{
    std::string Key;
    std::string Name;
    std::string Path;
    AbilityTimeDomain Domain = AbilityTimeDomain::Actor;
    std::uint32_t ProgressTicks = 0;
    std::vector<AbilityOpDecl> Enter;
    std::optional<AbilityPulseDecl> While;
    std::vector<AbilityOpDecl> Hold;
    std::vector<AbilityOpDecl> Exit;
    std::vector<AbilityPresentDecl> Present;
    std::vector<AbilityConditionDecl> Continue;
    std::vector<AbilityTransitionDecl> Then;
};

struct AbilityLocalDecl
{
    std::string Key;
    std::string Name;
    std::string Path;
    AbilityLocalType Type = AbilityLocalType::Float;
};

enum class AbilityCostTiming : std::uint8_t
{
    Start,
    Commit,
};

enum class AbilityCostRepeat : std::uint8_t
{
    Once,
    PerPulse,
};

struct AbilityCostGroupDecl
{
    std::string Key;
    std::string Name;
    std::string Path;
    AbilityCostTiming Timing = AbilityCostTiming::Commit;
    AbilityCostRepeat Repeat = AbilityCostRepeat::Once;
    // Effect definitions, by asset path.
    std::vector<std::string> Effects;
};

enum class AbilityCooldownStart : std::uint8_t
{
    OnStart,
    OnCommit,
    OnEnd,
};

struct AbilityCooldownDecl
{
    std::string Effect;
    AbilityCooldownStart Start = AbilityCooldownStart::OnStart;
    // OnCommit: the cost group whose commit starts it.
    std::string Group;
};

enum class AbilityInputPolicy : std::uint8_t
{
    // Starts on a press; a release is not the activation's business.
    Press,
    // Starts on a press, and the release is routed to it.
    PressRelease,
    // Starts on a press, and the next press ends it.
    Toggle,
};

enum class AbilityConsume : std::uint8_t
{
    Exclusive,
    Observe,
};

enum class AbilityWhenBlocked : std::uint8_t
{
    Consume,
    FallThrough,
};

enum class AbilityInterrupt : std::uint8_t
{
    Cancel,
    Release,
};

// Every field is optional so that a grant entry overrides only what it states.
struct AbilityBindingDecl
{
    std::optional<std::string> Action{};
    std::optional<AbilityInputPolicy> Policy{};
    std::optional<std::int32_t> Priority{};
    std::optional<AbilityConsume> Consume{};
    std::optional<AbilityWhenBlocked> WhenBlocked{};
    std::optional<AbilityTagQueryDecl> Context{};
    std::optional<AbilityInterrupt> OnInterrupt{};

    friend bool operator==(const AbilityBindingDecl&, const AbilityBindingDecl&) = default;
};

enum class AbilityPredictionLevel : std::uint8_t
{
    None,
    Presentation,
    Simulation,
};

struct AbilityDeclaration
{
    std::string DisplayName;
    std::string Category;
    std::optional<AbilityBindingDecl> InputDefault;
    std::vector<AbilityConditionDecl> Activation;
    std::vector<AbilityConditionDecl> Continuation;
    std::vector<AbilityConditionDecl> CancelOn;
    std::vector<std::string> OwnedTags;
    std::vector<std::string> CancelTags;
    std::uint8_t Instances = 1;
    std::vector<AbilityLocalDecl> Locals;
    std::vector<AbilityCostGroupDecl> Costs;
    std::optional<AbilityCooldownDecl> Cooldown;
    std::vector<AbilityPhaseDecl> Phases;
    AbilityPredictionLevel Prediction = AbilityPredictionLevel::Presentation;
    bool CancelOnReload = true;
};
