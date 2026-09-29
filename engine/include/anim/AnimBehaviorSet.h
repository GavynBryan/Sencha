#pragma once

#include <anim/AnimTypes.h>
#include <anim/AnimationClip.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

inline constexpr std::string_view kAnimBehaviorSetType = "animation.behavior_set";

enum class AnimBehaviorKind : std::uint8_t
{
    Cyclic,
    OneShot,
    Flow,
    // A cyclic pose that rests at its last frame: an open door, a body.
    Hold,
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

// Invoked when a layer enters or leaves a behavior, with the behavior's tag as the
// `behavior` input.
struct AnimLifecycleDecl
{
    std::string Binding;
    AnimEventScope Scope = AnimEventScope::Cosmetic;
};

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
    // Clip speed (negative plays backwards, zero holds) and start, in seconds.
    // Flows play on the tick clock and take neither.
    float Rate = 1.0f;
    float StartSeconds = 0.0f;
    // Layer weight below which cosmetic events on this behavior's content do not fire.
    float EventWeight = 0.5f;
    std::optional<AnimLifecycleDecl> OnEntered;
    std::optional<AnimLifecycleDecl> OnExited;
};

struct AnimBehaviorSet
{
    std::vector<AnimBehaviorDecl> Behaviors;
};

void RegisterAnimBehaviorSet(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);

// The `in`, `in_ms`, `out_ms`, `phase` record behaviors and blend overrides share.
// Reading fills only what is present.
[[nodiscard]] DataFieldSchema AnimBlendPolicySchema(std::string key, std::string label, std::string summary);
[[nodiscard]] bool ReadAnimBlendPolicy(const JsonValue* blend, const std::string& at, AnimBlendPolicy& out,
                                       std::string& error);
[[nodiscard]] std::string_view AnimBlendModeName(AnimBlendMode mode);
