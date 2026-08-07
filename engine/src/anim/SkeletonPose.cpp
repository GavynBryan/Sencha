#include <anim/SkeletonPose.h>

#include <cassert>

Mat4 JointTransformToMatrix(const JointTransform& transform)
{
    return Mat4::MakeTranslation(transform.Translation)
        * transform.Rotation.ToMat4()
        * Mat4::MakeScale(transform.Scale);
}

void BuildModelSpacePose(const SkeletonData& skeleton,
                         std::span<const JointTransform> localPose,
                         std::span<Mat4> modelPose)
{
    assert(localPose.size() == skeleton.Joints.size());
    assert(modelPose.size() >= skeleton.Joints.size());

    for (size_t jointIndex = 0; jointIndex < skeleton.Joints.size(); ++jointIndex)
    {
        const Mat4 local = JointTransformToMatrix(localPose[jointIndex]);
        const int32_t parent = skeleton.Joints[jointIndex].ParentIndex;
        modelPose[jointIndex] = parent >= 0 ? modelPose[static_cast<size_t>(parent)] * local : local;
    }
}

void BuildSkinningPalette(const SkeletonData& skeleton,
                          std::span<const JointTransform> localPose,
                          std::span<Mat4> scratchModelPose,
                          std::span<Mat4> skinningPalette)
{
    assert(skinningPalette.size() >= skeleton.Joints.size());
    BuildModelSpacePose(skeleton, localPose, scratchModelPose);

    for (size_t jointIndex = 0; jointIndex < skeleton.Joints.size(); ++jointIndex)
        skinningPalette[jointIndex] = scratchModelPose[jointIndex] * skeleton.Joints[jointIndex].InverseBind;
}
