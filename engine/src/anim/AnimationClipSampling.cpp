#include <anim/AnimationClipSampling.h>

#include <algorithm>
#include <cassert>

namespace
{
// The pair of keys `time` falls between, and how far between them it sits.
// Times are strictly ascending (format invariant), so the search is a plain
// upper bound; before the first key and after the last one both clamp,
// which is what makes a clip hold its ends rather than extrapolate.
struct KeyBlend
{
    std::size_t Lower = 0;
    std::size_t Upper = 0;
    float Alpha = 0.0f;
};

KeyBlend FindKeys(std::span<const float> times, float time)
{
    if (times.size() == 1 || time <= times.front())
        return KeyBlend{ 0, 0, 0.0f };
    if (time >= times.back())
    {
        const std::size_t last = times.size() - 1;
        return KeyBlend{ last, last, 0.0f };
    }

    const auto upper = std::upper_bound(times.begin(), times.end(), time);
    const auto lower = std::prev(upper);
    const std::size_t lowerIndex =
        static_cast<std::size_t>(std::distance(times.begin(), lower));
    const std::size_t upperIndex = lowerIndex + 1;
    const float span = times[upperIndex] - times[lowerIndex];
    // Strictly ascending times make the span positive; the guard costs
    // nothing and keeps a degenerate cooked file from producing a NaN pose.
    const float alpha = span > 0.0f ? (time - times[lowerIndex]) / span : 0.0f;
    return KeyBlend{ lowerIndex, upperIndex, alpha };
}

Vec3d SampleVec3(const AnimationJointTrack& track, const KeyBlend& keys)
{
    const auto at = [&](std::size_t key)
    {
        return Vec3d(track.Values[key * 3 + 0],
                     track.Values[key * 3 + 1],
                     track.Values[key * 3 + 2]);
    };
    const Vec3d lower = at(keys.Lower);
    if (keys.Alpha <= 0.0f || track.Interpolation == AnimationInterpolation::Step)
        return lower;
    const Vec3d upper = at(keys.Upper);
    return lower + (upper - lower) * keys.Alpha;
}

Quat<float> SampleQuat(const AnimationJointTrack& track, const KeyBlend& keys)
{
    const auto at = [&](std::size_t key)
    {
        return Quat<float>(track.Values[key * 4 + 0],
                           track.Values[key * 4 + 1],
                           track.Values[key * 4 + 2],
                           track.Values[key * 4 + 3]);
    };
    const Quat<float> lower = at(keys.Lower);
    if (keys.Alpha <= 0.0f || track.Interpolation == AnimationInterpolation::Step)
        return lower;
    return Quat<float>::Slerp(lower, at(keys.Upper), keys.Alpha);
}
} // namespace

void SampleAnimationClip(const AnimationClipData& clip,
                         const SkeletonData& skeleton,
                         float timeSeconds,
                         std::vector<Transform3f>& out)
{
    out.assign(skeleton.Joints.size(), Transform3f{});
    for (std::size_t joint = 0; joint < skeleton.Joints.size(); ++joint)
    {
        out[joint].Position = skeleton.Joints[joint].BindTranslation;
        out[joint].Rotation = skeleton.Joints[joint].BindRotation;
        out[joint].Scale = skeleton.Joints[joint].BindScale;
    }

    for (const AnimationJointTrack& track : clip.Tracks)
    {
        if (track.JointIndex >= out.size() || track.TimesSeconds.empty())
            continue;
        Transform3f& pose = out[track.JointIndex];
        const KeyBlend keys = FindKeys(track.TimesSeconds, timeSeconds);
        switch (track.Path)
        {
        case AnimationChannelPath::Translation:
            pose.Position = SampleVec3(track, keys);
            break;
        case AnimationChannelPath::Rotation:
            pose.Rotation = SampleQuat(track, keys);
            break;
        case AnimationChannelPath::Scale:
            pose.Scale = SampleVec3(track, keys);
            break;
        }
    }
}

void BuildPosedModelTransforms(const SkeletonData& skeleton,
                               std::span<const Transform3f> localPose,
                               std::vector<Mat4>& out)
{
    assert(localPose.size() == skeleton.Joints.size());
    out.clear();
    out.reserve(skeleton.Joints.size());
    for (std::size_t joint = 0; joint < skeleton.Joints.size(); ++joint)
    {
        const Mat4 localMatrix = localPose[joint].ToMat4();
        const int32_t parent = skeleton.Joints[joint].ParentIndex;
        if (parent < 0)
        {
            out.push_back(localMatrix);
            continue;
        }
        // Parents strictly precede children (format invariant), so the
        // parent's model transform is already in `out`.
        assert(static_cast<std::size_t>(parent) < out.size());
        out.push_back(out[static_cast<std::size_t>(parent)] * localMatrix);
    }
}
