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

std::string_view AnimEventScopeName(AnimEventScope scope)
{
    switch (scope)
    {
    case AnimEventScope::Cosmetic: return "cosmetic";
    case AnimEventScope::Gameplay: return "gameplay";
    }
    return "unknown";
}

bool ValidateAnimationClipEvent(const AnimationClipEvent& event, std::string* error)
{
    const auto fail = [&](std::string_view why) {
        return Fail(error, std::format("event {}: {}", event.Key, why));
    };
    if (event.Key == 0)
        return Fail(error, "an event needs a nonzero key");
    if (!std::isfinite(event.Time) || event.Time < 0.0f || event.Time > 1.0f)
        return fail("time must be normalized, between 0 and 1");
    if (event.Binding.empty())
        return fail("names no binding");
    if (event.Scope != AnimEventScope::Cosmetic && event.Scope != AnimEventScope::Gameplay)
        return fail("unknown scope");
    if (event.MinWeight.has_value())
    {
        if (event.Scope != AnimEventScope::Cosmetic)
            return fail("only a cosmetic event has a minimum weight");
        if (!std::isfinite(*event.MinWeight) || *event.MinWeight < 0.0f || *event.MinWeight > 1.0f)
            return fail("minimum weight must be between 0 and 1");
    }
    if (event.Inputs.size() > kAnimEventMaxInputs)
        return fail(std::format("supplies {} inputs; an event supplies at most {}", event.Inputs.size(),
                                kAnimEventMaxInputs));
    for (size_t index = 0; index < event.Inputs.size(); ++index)
    {
        const VerbBindingArgument& input = event.Inputs[index];
        if (input.Key.empty())
            return fail("an input has no name");
        for (size_t other = index + 1; other < event.Inputs.size(); ++other)
            if (event.Inputs[other].Key == input.Key)
                return fail(std::format("input '{}' is supplied twice", input.Key));
        if (input.Source != VerbArgumentSource::Literal && input.Source != VerbArgumentSource::Tag)
            return fail(std::format("input '{}' is a reference; asset and entity references are "
                                    "constants on the binding",
                                    input.Key));
        if (input.Source == VerbArgumentSource::Tag && input.Text.empty())
            return fail(std::format("input '{}' names no tag", input.Key));
    }
    return true;
}

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

    if (clip.Events.size() > kAnimClipMaxEvents)
        return Fail(error, std::format("clip has {} events; a clip holds at most {}", clip.Events.size(),
                                       kAnimClipMaxEvents));
    for (size_t index = 0; index < clip.Events.size(); ++index)
    {
        const AnimationClipEvent& event = clip.Events[index];
        if (!ValidateAnimationClipEvent(event, error))
            return false;
        for (size_t other = 0; other < index; ++other)
            if (clip.Events[other].Key == event.Key)
                return Fail(error, std::format("event key {} is used twice", event.Key));
        if (index > 0)
        {
            const AnimationClipEvent& previous = clip.Events[index - 1];
            if (event.Time < previous.Time || (event.Time == previous.Time && event.Key < previous.Key))
                return Fail(error, std::format("event {} is out of order; events are ordered by time, "
                                               "then key",
                                               event.Key));
        }
    }

    return true;
}
