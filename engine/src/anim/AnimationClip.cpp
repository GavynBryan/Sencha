#include <anim/AnimationClip.h>

#include <anim/Skeleton.h>
#include <core/assets/AssetPath.h>

#include <cmath>
#include <format>

namespace
{
    bool Fail(std::string* error, std::string message)
    {
        if (error)
            *error = std::move(message);
        return false;
    }
} // namespace

uint32_t AnimationChannelComponentCount(AnimationChannelPath path)
{
    return path == AnimationChannelPath::Rotation ? 4u : 3u;
}

bool ValidateAnimationClipData(const AnimationClipData& clip, std::string* error)
{
    if (!IsValidAssetPath(clip.SkeletonPath))
        return Fail(error, "clip skeleton path must be an asset:// path");
    if (clip.Tracks.empty())
        return Fail(error, "clip has no tracks");
    if (!std::isfinite(clip.DurationSeconds) || clip.DurationSeconds <= 0.0f)
        return Fail(error, "clip duration must be finite and positive");

    for (size_t trackIndex = 0; trackIndex < clip.Tracks.size(); ++trackIndex)
    {
        const AnimationJointTrack& track = clip.Tracks[trackIndex];
        const auto fail = [&](std::string_view why) {
            return Fail(error, std::format("track {}: {}", trackIndex, why));
        };

        if (track.JointIndex >= kMaxSkeletonJoints)
            return fail("joint index exceeds the skeleton joint cap");
        if (track.Path != AnimationChannelPath::Translation
            && track.Path != AnimationChannelPath::Rotation
            && track.Path != AnimationChannelPath::Scale)
            return fail("unknown channel path");
        if (track.Interpolation != AnimationInterpolation::Linear
            && track.Interpolation != AnimationInterpolation::Step)
            return fail("unknown interpolation");
        if (track.TimesSeconds.empty())
            return fail("track has no keys");

        const uint32_t components = AnimationChannelComponentCount(track.Path);
        if (track.Values.size() != track.TimesSeconds.size() * components)
            return fail("value count does not match key count");

        float previous = -1.0f;
        for (const float time : track.TimesSeconds)
        {
            if (!std::isfinite(time) || time < 0.0f)
                return fail("key times must be finite and non-negative");
            if (time <= previous)
                return fail("key times must be strictly ascending");
            previous = time;
        }
        if (previous > clip.DurationSeconds)
            return fail("last key time exceeds the clip duration");

        for (const float value : track.Values)
        {
            if (!std::isfinite(value))
                return fail("values must be finite");
        }

        if (track.Path == AnimationChannelPath::Rotation)
        {
            for (size_t key = 0; key < track.TimesSeconds.size(); ++key)
            {
                const float* q = track.Values.data() + key * 4;
                const float lengthSq = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
                if (std::abs(lengthSq - 1.0f) > 1e-3f)
                    return fail("rotation keys must be unit quaternions");
            }
        }
    }

    return true;
}
