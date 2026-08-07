#pragma once

#include <anim/Skeleton.h>
#include <math/Mat.h>
#include <math/Quat.h>
#include <math/Vec.h>

#include <span>

struct JointTransform
{
    Vec3d Translation{};
    Quat<float> Rotation{};
    Vec3d Scale = Vec3d(1.0f, 1.0f, 1.0f);
};

[[nodiscard]] Mat4 JointTransformToMatrix(const JointTransform& transform);
void BuildModelSpacePose(const SkeletonData& skeleton,
                         std::span<const JointTransform> localPose,
                         std::span<Mat4> modelPose);
void BuildSkinningPalette(const SkeletonData& skeleton,
                          std::span<const JointTransform> localPose,
                          std::span<Mat4> scratchModelPose,
                          std::span<Mat4> skinningPalette);
