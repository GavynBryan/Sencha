#include <anim/AnimPoseComposition.h>

#include <anim/AnimationClipSampling.h>

#include <algorithm>

namespace
{
    bool Covers(std::span<const std::uint8_t> mask, std::size_t joint)
    {
        return mask.empty() || (joint < mask.size() && mask[joint] != 0);
    }

    float Ratio(float value, float reference)
    {
        return reference != 0.0f ? value / reference : 1.0f;
    }
}

void AnimBindPose(const SkeletonData& skeleton, std::vector<Transform3f>& out)
{
    // An empty clip samples to bind.
    static const AnimationClipData kBind;
    SampleAnimationClip(kBind, skeleton, 0.0f, out);
}

void ComposeAnimPose(const SkeletonData& skeleton, std::span<const AnimPoseLayer> layers,
                     std::vector<Transform3f>& out)
{
    AnimBindPose(skeleton, out);
    const std::size_t joints = skeleton.Joints.size();
    for (const AnimPoseLayer& layer : layers)
    {
        const float weight = std::clamp(layer.Weight, 0.0f, 1.0f);
        if (layer.Pose.size() < joints || weight <= 0.0f)
            continue;

        if (layer.Mode == AnimLayerMode::Override)
        {
            for (std::size_t j = 0; j < joints; ++j)
                if (Covers(layer.Mask, j))
                    out[j] = weight >= 1.0f ? layer.Pose[j] : Transform3f::Interpolate(out[j], layer.Pose[j], weight);
            continue;
        }

        if (layer.Reference.size() < joints)
            continue;
        for (std::size_t j = 0; j < joints; ++j)
        {
            if (!Covers(layer.Mask, j))
                continue;
            const Transform3f& sample = layer.Pose[j];
            const Transform3f& reference = layer.Reference[j];
            Transform3f& pose = out[j];
            pose.Position += (sample.Position - reference.Position) * weight;
            const Quat<float> delta = reference.Rotation.Inverse() * sample.Rotation;
            pose.Rotation = (pose.Rotation * Quat<float>::Slerp(Quat<float>::Identity(), delta, weight)).Normalized();
            const Vec3d ratio(Ratio(sample.Scale.X, reference.Scale.X), Ratio(sample.Scale.Y, reference.Scale.Y),
                              Ratio(sample.Scale.Z, reference.Scale.Z));
            pose.Scale = Vec3d(pose.Scale.X * (1.0f + (ratio.X - 1.0f) * weight),
                               pose.Scale.Y * (1.0f + (ratio.Y - 1.0f) * weight),
                               pose.Scale.Z * (1.0f + (ratio.Z - 1.0f) * weight));
        }
    }
}
