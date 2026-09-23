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

// Where an event may be produced. Neither is an authority claim: a mutating
// verb still checks IsSimulationAuthority and validates its instigator.
enum class AnimEventScope : uint8_t
{
    // On whichever machine presents the pose.
    Cosmetic = 0,
    // Only in a World with simulation authority, never on a client.
    Gameplay = 1,
};

[[nodiscard]] std::string_view AnimEventScopeName(AnimEventScope scope);

// The content budget for one event's inputs. Anything larger belongs in
// gameplay state, a fact, or a constant on the binding.
inline constexpr std::size_t kAnimEventMaxInputs = 4;
inline constexpr std::size_t kAnimClipMaxEvents = 256;

// A timeline mark that produces an invocation through an authored binding.
// The clip names the binding by its key and supplies only the binding's
// declared inputs; the verb, its constants and its target are the binding's.
// Nothing here is a verb id, a tag id or a callback, so one clip means the
// same thing in every World that binds it.
struct AnimationClipEvent
{
    // Stable per clip and never reused: what selection, undo and the decision
    // log name an event by, whatever its time or position.
    uint32_t Key = 0;
    // Diagnostics and display only.
    std::string Name;
    // Normalized clip time, 0..1.
    float Time = 0.0f;
    // The authored binding key, resolved per World through the rig's bindings.
    std::string Binding;
    AnimEventScope Scope = AnimEventScope::Cosmetic;
    // Cosmetic only: the layer weight below which the event is not produced.
    // Absent means the playing behavior's threshold applies.
    std::optional<float> MinWeight;
    // One value per binding input, by the input's name. Only constants and
    // tags: asset and entity references are properties of the binding.
    std::vector<VerbBindingArgument> Inputs;
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
};

// The invariants one event must meet, shared by the clip validator and the
// cook that parses authored events. Errors travel in `error`.
[[nodiscard]] bool ValidateAnimationClipEvent(const AnimationClipEvent& event, std::string* error = nullptr);

// Format invariants (the runtime never fixes data): at least one track,
// strictly ascending finite times, value counts matching key counts, unit
// rotation keys, duration covering the last key, and events that each meet
// ValidateAnimationClipEvent, carry distinct keys and are in order. Joint
// indices are bounded by kMaxSkeletonJoints here and by the actual skeleton at
// commit.
[[nodiscard]] bool ValidateAnimationClipData(const AnimationClipData& clip,
                                             std::string* error = nullptr);
