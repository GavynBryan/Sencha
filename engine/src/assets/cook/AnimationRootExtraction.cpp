#include <assets/cook/AnimationRootExtraction.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace
{
    AnimationJointTrack* FindTrack(AnimationClipData& clip, std::uint32_t joint, AnimationChannelPath path)
    {
        for (AnimationJointTrack& track : clip.Tracks)
            if (track.JointIndex == joint && track.Path == path)
                return &track;
        return nullptr;
    }

    // Where a track is at `time`: its bracketing keys, and how far between.
    struct Bracket
    {
        std::size_t A = 0;
        std::size_t B = 0;
        float T = 0.0f;
    };

    Bracket BracketOf(const AnimationJointTrack& track, float time)
    {
        const std::vector<float>& times = track.TimesSeconds;
        if (time <= times.front())
            return {};
        if (time >= times.back())
            return { times.size() - 1, times.size() - 1, 0.0f };
        const std::size_t b =
            static_cast<std::size_t>(std::upper_bound(times.begin(), times.end(), time) - times.begin());
        const std::size_t a = b - 1;
        const float t = track.Interpolation == AnimationInterpolation::Step
            ? 0.0f
            : (time - times[a]) / (times[b] - times[a]);
        return { a, b, t };
    }

    Vec3d TranslationAt(const AnimationJointTrack* track, const Vec3d& rest, float time)
    {
        if (track == nullptr)
            return rest;
        const Bracket at = BracketOf(*track, time);
        const float* a = track->Values.data() + at.A * 3;
        const float* b = track->Values.data() + at.B * 3;
        return Vec3d{ a[0] + (b[0] - a[0]) * at.T, a[1] + (b[1] - a[1]) * at.T, a[2] + (b[2] - a[2]) * at.T };
    }

    Quat<float> KeyRotation(const AnimationJointTrack& track, std::size_t key)
    {
        const float* q = track.Values.data() + key * 4;
        return Quat<float>{ q[0], q[1], q[2], q[3] };
    }

    Quat<float> RotationAt(const AnimationJointTrack* track, const Quat<float>& rest, float time)
    {
        if (track == nullptr)
            return rest;
        const Bracket at = BracketOf(*track, time);
        return Quat<float>::Slerp(KeyRotation(*track, at.A), KeyRotation(*track, at.B), at.T);
    }

    // The heading of `turn` about +Y: 0 facing -Z, positive turning towards -X.
    float HeadingOf(const Quat<float>& turn)
    {
        const Vec3d forward = turn.RotateVector(Vec3d{ 0.0f, 0.0f, -1.0f });
        return std::atan2(-forward.X, -forward.Z);
    }

    // `angle` moved by whole turns to within half a turn of `near`.
    float Unwrap(float angle, float near)
    {
        constexpr float turn = 2.0f * std::numbers::pi_v<float>;
        while (angle - near > std::numbers::pi_v<float>)
            angle -= turn;
        while (angle - near < -std::numbers::pi_v<float>)
            angle += turn;
        return angle;
    }
}

bool ExtractAnimationRootMotion(AnimationClipData& clip, const SkeletonData& skeleton, std::string* error)
{
    std::uint32_t root = 0;
    std::size_t roots = 0;
    for (std::size_t j = 0; j < skeleton.Joints.size(); ++j)
        if (skeleton.Joints[j].ParentIndex < 0)
        {
            root = static_cast<std::uint32_t>(j);
            ++roots;
        }
    if (roots != 1)
    {
        if (error != nullptr)
            *error = std::format("the skeleton has {} root joints; root motion needs exactly one to carry the "
                                 "character",
                                 roots);
        return false;
    }

    const SkeletonJoint& rest = skeleton.Joints[root];
    AnimationJointTrack* translation = FindTrack(clip, root, AnimationChannelPath::Translation);
    AnimationJointTrack* rotation = FindTrack(clip, root, AnimationChannelPath::Rotation);

    // Every time either channel keys, so the curve is exact wherever the
    // source was.
    std::vector<float> times;
    for (const AnimationJointTrack* track : { static_cast<const AnimationJointTrack*>(translation),
                                              static_cast<const AnimationJointTrack*>(rotation) })
        if (track != nullptr)
            times.insert(times.end(), track->TimesSeconds.begin(), track->TimesSeconds.end());
    std::ranges::sort(times);
    times.erase(std::unique(times.begin(), times.end()), times.end());
    if (times.empty())
        times.push_back(0.0f);

    const Vec3d start = TranslationAt(translation, rest.BindTranslation, times.front());
    const Quat<float> startTurn = RotationAt(rotation, rest.BindRotation, times.front()).Inverse();
    const auto headingAt = [&](const Quat<float>& orientation, float near) {
        return Unwrap(HeadingOf(orientation * startTurn), near);
    };

    AnimationRootCurve curve;
    curve.TimesSeconds = times;
    float previous = 0.0f;
    for (const float time : times)
    {
        const Vec3d at = TranslationAt(translation, rest.BindTranslation, time);
        previous = headingAt(RotationAt(rotation, rest.BindRotation, time), previous);
        curve.Values.insert(curve.Values.end(), { at.X - start.X, at.Z - start.Z, previous });
    }

    // What stays in the pose: the root over its starting point, facing its
    // starting way.
    if (translation != nullptr)
    {
        AnimationJointTrack& track = *translation;
        for (std::size_t key = 0; key < track.TimesSeconds.size(); ++key)
        {
            track.Values[key * 3] = start.X;
            track.Values[key * 3 + 2] = start.Z;
        }
    }
    if (rotation != nullptr)
    {
        AnimationJointTrack& track = *rotation;
        float near = 0.0f;
        for (std::size_t key = 0; key < track.TimesSeconds.size(); ++key)
        {
            const Quat<float> orientation = KeyRotation(track, key);
            near = headingAt(orientation, near);
            const Quat<float> kept =
                (Quat<float>::FromAxisAngle(Vec3d{ 0.0f, 1.0f, 0.0f }, -near) * orientation).Normalized();
            float* q = track.Values.data() + key * 4;
            q[0] = kept.X;
            q[1] = kept.Y;
            q[2] = kept.Z;
            q[3] = kept.W;
        }
    }
    clip.Root = std::move(curve);
    return true;
}
