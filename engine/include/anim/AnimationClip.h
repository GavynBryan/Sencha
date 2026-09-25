#pragma once

#include <authored/VerbBinding.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

//=============================================================================
// AnimationClip (docs/assets/pipeline.md, Decision J)
//
// Per-joint animation tracks referencing their skeleton by AssetRef. This
// is the authored data as the cook extracted it from glTF — keyframed,
// linear or step interpolation, no resampling and no compression. Clip
// storage (sampled vs. keyframed) and compression are explicitly deferred
// to the animation runtime plan; the .sanim version field is the room
// those decisions get to move in.
//=============================================================================

enum class AnimationChannelPath : uint32_t
{
    Translation = 0, // Vec3 values
    Rotation = 1,    // unit quaternion values (x, y, z, w)
    Scale = 2,       // Vec3 values
};

enum class AnimationInterpolation : uint32_t
{
    Linear = 0,
    Step = 1,
};

// Returns 3 for translation/scale, 4 for rotation.
[[nodiscard]] uint32_t AnimationChannelComponentCount(AnimationChannelPath path);

struct AnimationJointTrack
{
    // Skeleton-local joint index (Decision N: resolved at cook).
    uint32_t JointIndex = 0;

    AnimationChannelPath Path = AnimationChannelPath::Translation;
    AnimationInterpolation Interpolation = AnimationInterpolation::Linear;

    // Strictly ascending, non-negative key times in seconds, and the flat
    // value stream: ComponentCount(Path) floats per key.
    std::vector<float> TimesSeconds;
    std::vector<float> Values;
};

// Where an event may be produced. Neither scope is an authority claim: a
// mutating verb still checks IsSimulationAuthority and its instigator.
enum class AnimEventScope : uint8_t
{
    // On whichever machine presents the pose.
    Cosmetic = 0,
    // Only in a World with simulation authority, never on a client.
    Gameplay = 1,
};

[[nodiscard]] std::string_view AnimEventScopeName(AnimEventScope scope);

// Anything larger belongs in gameplay state, a fact, or a binding constant.
inline constexpr std::size_t kAnimEventMaxInputs = 4;
inline constexpr std::size_t kAnimClipMaxEvents = 256;

// A timeline mark invoking an authored binding by key. It holds no verb id, tag
// id or callback, so one clip means the same thing in every World that binds it.
struct AnimationClipEvent
{
    // Stable per clip and never reused, whatever the event's time or position.
    uint32_t Key = 0;
    // Diagnostics and display only.
    std::string Name;
    // Normalized clip time, 0..1.
    float Time = 0.0f;
    // Resolved per World through the rig's bindings.
    std::string Binding;
    AnimEventScope Scope = AnimEventScope::Cosmetic;
    // Cosmetic only; absent uses the playing behavior's threshold.
    std::optional<float> MinWeight;
    // By input name. Constants and tags only: asset and entity references belong
    // to the binding.
    std::vector<VerbBindingArgument> Inputs;
};

// Root travel extracted at cook: planar x, z and unwrapped yaw about +Y, in the
// skeleton's model space relative to the clip's start. Linear between keys.
struct AnimationRootCurve
{
    std::vector<float> TimesSeconds;
    // x, z, yaw per key.
    std::vector<float> Values;
};

struct AnimationClipData
{
    // The skeleton this clip poses ("asset://..."). Resolved at commit;
    // track joint indices are validated against it there, because the clip
    // alone cannot know the skeleton's joint count.
    std::string SkeletonPath;

    float DurationSeconds = 0.0f;

    std::vector<AnimationJointTrack> Tracks;

    // Ordered by time, then key.
    std::vector<AnimationClipEvent> Events;

    // Present when the clip was cooked with its root motion extracted; the
    // root joint's tracks then carry no planar travel and no yaw.
    std::optional<AnimationRootCurve> Root;
};

// Shared by the clip validator and the cook that parses authored events.
[[nodiscard]] bool ValidateAnimationClipEvent(const AnimationClipEvent& event, std::string* error = nullptr);

// Format invariants the runtime never repairs: ascending finite key times, matching
// value counts, unit rotations, a duration covering every key (root curve too) and
// valid events with distinct keys. The real skeleton bounds joints at commit.
[[nodiscard]] bool ValidateAnimationClipData(const AnimationClipData& clip,
                                             std::string* error = nullptr);
