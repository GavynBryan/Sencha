#pragma once

#include <cstdint>
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

struct AnimationClipData
{
    // The skeleton this clip poses ("asset://..."). Resolved at commit;
    // track joint indices are validated against it there, because the clip
    // alone cannot know the skeleton's joint count.
    std::string SkeletonPath;

    float DurationSeconds = 0.0f;

    std::vector<AnimationJointTrack> Tracks;
};

// Format invariants (the runtime never fixes data): at least one track,
// strictly ascending finite times, value counts matching key counts, unit
// rotation keys, duration covering the last key. Joint indices are bounded
// by kMaxSkeletonJoints here and by the actual skeleton at commit.
[[nodiscard]] bool ValidateAnimationClipData(const AnimationClipData& clip,
                                             std::string* error = nullptr);
