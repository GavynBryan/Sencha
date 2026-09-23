#pragma once

#include <anim/AnimationClip.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// Behavior set (`animation.behavior_set`)
//
// Behaviors are the boundary between shared selection logic and one rig's
// content. A behavior is a gameplay tag (Anim.Locomotion.Sprint,
// Anim.Action.Reload) plus the policy that says how it plays: its kind, how a
// change to it is blended, whether it latches, how a late joiner sees it.
// Selectors name behaviors and nothing below them; slot maps turn a behavior
// into content.
//
// A rig lists behavior sets in order. A later set may add behaviors or
// override an earlier set's policy for a tag, which is how a game or mod
// changes a base behavior without editing it.
//=============================================================================

inline constexpr std::string_view kAnimBehaviorSetType = "animation.behavior_set";

enum class AnimBehaviorKind : std::uint8_t
{
    Cyclic,
    OneShot,
    Flow,
    // A cyclic pose that rests at its last frame: an open door, a body.
    Hold,
};

enum class AnimBlendMode : std::uint8_t
{
    Inertialize,
    Crossfade,
    Snap,
};

enum class AnimPhasePolicy : std::uint8_t
{
    Reset,
    Carry,
};

struct AnimBlendPolicy
{
    AnimBlendMode In = AnimBlendMode::Inertialize;
    float InMs = 150.0f;
    // Crossfade only.
    float OutMs = 0.0f;
    AnimPhasePolicy Phase = AnimPhasePolicy::Reset;
};

enum class AnimLatchMode : std::uint8_t
{
    None,
    UntilComplete,
    UntilRequestEnds,
};

enum class AnimInterruptKind : std::uint8_t
{
    PriorityAtLeast,
    Tags,
    Never,
};

enum class AnimInterruptAction : std::uint8_t
{
    Abort,
    CancelSection,
};

enum class AnimRequestCancelAction : std::uint8_t
{
    Finish,
    Abort,
    CancelSection,
};

struct AnimLatchPolicy
{
    AnimLatchMode Mode = AnimLatchMode::None;
    AnimInterruptKind InterruptibleBy = AnimInterruptKind::PriorityAtLeast;
    // PriorityAtLeast: the band a rule must reach to pre-empt the latch.
    std::int32_t Priority = 100;
    // Tags: behaviors (hierarchically) that may pre-empt it.
    std::vector<std::string> Tags;
    AnimInterruptAction OnInterrupt = AnimInterruptAction::Abort;
    AnimRequestCancelAction OnRequestCancel = AnimRequestCancelAction::Finish;
};

enum class AnimLateJoin : std::uint8_t
{
    Skip,
    SnapToEnd,
    Reconstruct,
};

[[nodiscard]] std::string_view AnimBehaviorKindName(AnimBehaviorKind kind);
[[nodiscard]] std::string_view AnimLatchModeName(AnimLatchMode mode);

// A binding invoked when a layer enters or leaves a behavior. The event
// supplies one input, `behavior`, carrying the behavior's tag, to a binding
// that declares it; everything else is the binding's.
struct AnimLifecycleDecl
{
    std::string Binding;
    AnimEventScope Scope = AnimEventScope::Cosmetic;
};

// The one input a lifecycle event supplies.
inline constexpr std::string_view kAnimLifecycleBehaviorInput = "behavior";

struct AnimBehaviorDecl
{
    std::string Tag;
    AnimBehaviorKind Kind = AnimBehaviorKind::Cyclic;
    AnimBlendPolicy Blend;
    AnimLatchPolicy Latch;
    AnimLateJoin LateJoin = AnimLateJoin::Skip;
    // Behaviors in one sync group keep phase across a change; empty for none.
    std::string SyncGroup;
    bool RootMotion = false;
    // The layer weight below which cosmetic events on this behavior's content
    // do not fire.
    float EventWeight = 0.5f;
    std::optional<AnimLifecycleDecl> OnEntered;
    std::optional<AnimLifecycleDecl> OnExited;
};

struct AnimBehaviorSet
{
    std::vector<AnimBehaviorDecl> Behaviors;
};

void RegisterAnimBehaviorSet(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);
