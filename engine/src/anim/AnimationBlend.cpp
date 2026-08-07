#include <anim/AnimationBlend.h>

#include <algorithm>
#include <cassert>

namespace
{
    [[nodiscard]] JointTransform BlendJoint(const JointTransform& a,
                                            const JointTransform& b,
                                            float alpha)
    {
        const float clamped = std::clamp(alpha, 0.0f, 1.0f);
        return JointTransform{
            .Translation = Vec3d::Lerp(a.Translation, b.Translation, clamped),
            .Rotation = Quat<float>::Nlerp(a.Rotation, b.Rotation, clamped),
            .Scale = Vec3d::Lerp(a.Scale, b.Scale, clamped)
        };
    }
} // namespace

void BlendPoses(std::span<const JointTransform> a,
                std::span<const JointTransform> b,
                float alpha,
                std::span<JointTransform> out)
{
    assert(a.size() == b.size());
    assert(out.size() >= a.size());

    for (size_t jointIndex = 0; jointIndex < a.size(); ++jointIndex)
        out[jointIndex] = BlendJoint(a[jointIndex], b[jointIndex], alpha);
}

void BlendPosesWeighted(std::span<const std::span<const JointTransform>> poses,
                        std::span<const float> weights,
                        std::span<JointTransform> out)
{
    assert(poses.size() == weights.size());
    assert(!poses.empty());

    const size_t jointCount = poses[0].size();
    assert(out.size() >= jointCount);
    for (std::span<const JointTransform> pose : poses)
        assert(pose.size() == jointCount);

    for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
    {
        Vec3d translation{};
        Vec3d scale{};
        Quat<float> rotation(0.0f, 0.0f, 0.0f, 0.0f);
        float totalWeight = 0.0f;

        for (size_t poseIndex = 0; poseIndex < poses.size(); ++poseIndex)
        {
            const float weight = std::max(weights[poseIndex], 0.0f);
            if (weight == 0.0f)
                continue;

            const JointTransform& joint = poses[poseIndex][jointIndex];
            Quat<float> sampleRotation = joint.Rotation;
            if (totalWeight > 0.0f && rotation.Dot(sampleRotation) < 0.0f)
                sampleRotation = -sampleRotation;

            translation += joint.Translation * weight;
            scale += joint.Scale * weight;
            rotation.X += sampleRotation.X * weight;
            rotation.Y += sampleRotation.Y * weight;
            rotation.Z += sampleRotation.Z * weight;
            rotation.W += sampleRotation.W * weight;
            totalWeight += weight;
        }

        assert(totalWeight > 0.0f);
        const float invWeight = 1.0f / totalWeight;
        out[jointIndex].Translation = translation * invWeight;
        out[jointIndex].Scale = scale * invWeight;
        out[jointIndex].Rotation = (rotation * invWeight).Normalized();
    }
}

void BlendPosesMasked(std::span<const JointTransform> base,
                      std::span<const JointTransform> layer,
                      float alpha,
                      std::span<const float> jointWeights,
                      std::span<JointTransform> out)
{
    assert(base.size() == layer.size());
    assert(jointWeights.size() >= base.size());
    assert(out.size() >= base.size());

    const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
    for (size_t jointIndex = 0; jointIndex < base.size(); ++jointIndex)
    {
        const float jointAlpha = clampedAlpha * std::clamp(jointWeights[jointIndex], 0.0f, 1.0f);
        out[jointIndex] = BlendJoint(base[jointIndex], layer[jointIndex], jointAlpha);
    }
}
