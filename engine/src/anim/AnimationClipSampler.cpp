#include <anim/AnimationClipSampler.h>

#include <algorithm>
#include <cassert>
#include <cmath>

namespace
{
    [[nodiscard]] size_t FindKeyIndex(std::span<const float> times, float timeSeconds)
    {
        auto upper = std::upper_bound(times.begin(), times.end(), timeSeconds);
        if (upper == times.begin())
            return 0;
        return static_cast<size_t>((upper - times.begin()) - 1);
    }

    [[nodiscard]] Vec3d ReadVec3(const std::vector<float>& values, size_t key)
    {
        const float* v = values.data() + key * 3;
        return Vec3d(v[0], v[1], v[2]);
    }

    [[nodiscard]] Quat<float> ReadQuat(const std::vector<float>& values, size_t key)
    {
        const float* q = values.data() + key * 4;
        return Quat<float>(q[0], q[1], q[2], q[3]);
    }

    [[nodiscard]] float SegmentAlpha(const AnimationJointTrack& track, size_t key, float timeSeconds)
    {
        if (key + 1 >= track.TimesSeconds.size())
            return 0.0f;
        const float t0 = track.TimesSeconds[key];
        const float t1 = track.TimesSeconds[key + 1];
        return std::clamp((timeSeconds - t0) / (t1 - t0), 0.0f, 1.0f);
    }
} // namespace

float WrapClipTime(float timeSeconds, float durationSeconds, bool looping)
{
    if (durationSeconds <= 0.0f || !std::isfinite(durationSeconds) || !std::isfinite(timeSeconds))
        return 0.0f;

    if (!looping)
        return std::clamp(timeSeconds, 0.0f, durationSeconds);

    float wrapped = std::fmod(timeSeconds, durationSeconds);
    if (wrapped < 0.0f)
        wrapped += durationSeconds;
    return wrapped;
}

void WriteBindPose(const SkeletonData& skeleton, std::span<JointTransform> pose)
{
    assert(pose.size() >= skeleton.Joints.size());
    for (size_t jointIndex = 0; jointIndex < skeleton.Joints.size(); ++jointIndex)
    {
        const SkeletonJoint& joint = skeleton.Joints[jointIndex];
        pose[jointIndex].Translation = joint.BindTranslation;
        pose[jointIndex].Rotation = joint.BindRotation;
        pose[jointIndex].Scale = joint.BindScale;
    }
}

void SampleClip(const AnimationClipData& clip,
                float timeSeconds,
                std::span<JointTransform> pose)
{
    for (const AnimationJointTrack& track : clip.Tracks)
    {
        assert(track.JointIndex < pose.size());
        if (track.TimesSeconds.empty())
            continue;

        const size_t key = FindKeyIndex(track.TimesSeconds, timeSeconds);
        JointTransform& joint = pose[track.JointIndex];

        if (track.Interpolation == AnimationInterpolation::Step || key + 1 >= track.TimesSeconds.size())
        {
            if (track.Path == AnimationChannelPath::Translation)
                joint.Translation = ReadVec3(track.Values, key);
            else if (track.Path == AnimationChannelPath::Scale)
                joint.Scale = ReadVec3(track.Values, key);
            else
                joint.Rotation = ReadQuat(track.Values, key);
            continue;
        }

        const float alpha = SegmentAlpha(track, key, timeSeconds);
        if (track.Path == AnimationChannelPath::Translation)
            joint.Translation = Vec3d::Lerp(ReadVec3(track.Values, key), ReadVec3(track.Values, key + 1), alpha);
        else if (track.Path == AnimationChannelPath::Scale)
            joint.Scale = Vec3d::Lerp(ReadVec3(track.Values, key), ReadVec3(track.Values, key + 1), alpha);
        else
            joint.Rotation = Quat<float>::Nlerp(ReadQuat(track.Values, key), ReadQuat(track.Values, key + 1), alpha);
    }
}
