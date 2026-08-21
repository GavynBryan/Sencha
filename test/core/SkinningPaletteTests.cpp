// The contract between a skeleton's cooked InverseBind and the bind TRS it was
// derived from: at bind pose, every skinning-palette entry is the identity
// matrix. That identity is what lets a rest-pose skinned mesh draw through the
// static path byte-exactly, and it is what both future skinning branches
// (vertex-shader and compute pre-skin) consume as their palette.

#include <gtest/gtest.h>

#include <anim/SkinningPalette.h>
#include <math/geometry/3d/Transform3d.h>

#include <cmath>

namespace
{

// A three-joint chain with rotation, translation, and non-uniform hierarchy --
// deliberately not axis-aligned, so a transposed or order-swapped multiply
// cannot cancel out to identity by accident.
SkeletonData MakeChain()
{
    SkeletonData skeleton;

    SkeletonJoint root;
    root.ParentIndex = -1;
    root.BindTranslation = Vec3d(1.0f, 2.0f, 3.0f);
    root.BindRotation = Quatf::FromAxisAngle(Vec3d::Up(), 0.7f);
    skeleton.Joints.push_back(root);

    SkeletonJoint mid;
    mid.ParentIndex = 0;
    mid.BindTranslation = Vec3d(0.0f, 1.5f, 0.0f);
    mid.BindRotation = Quatf::FromAxisAngle(Vec3d::Right(), -0.4f);
    skeleton.Joints.push_back(mid);

    SkeletonJoint tip;
    tip.ParentIndex = 1;
    tip.BindTranslation = Vec3d(0.25f, 1.0f, -0.5f);
    skeleton.Joints.push_back(tip);

    // The cook's half of the contract: InverseBind is the inverse of the
    // bind-pose model transform.
    std::vector<Mat4> model;
    BuildBindModelTransforms(skeleton, model);
    for (size_t joint = 0; joint < skeleton.Joints.size(); ++joint)
        skeleton.Joints[joint].InverseBind = model[joint].Inverse();

    return skeleton;
}

void ExpectNearIdentity(const Mat4& matrix)
{
    const Mat4 identity = Mat4::Identity();
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            EXPECT_NEAR(matrix[row][column], identity[row][column], 1e-5f);
}

} // namespace

TEST(SkinningPalette, BindPosePaletteIsIdentityForEveryJoint)
{
    const SkeletonData skeleton = MakeChain();

    std::vector<Mat4> model;
    BuildBindModelTransforms(skeleton, model);
    std::vector<Mat4> palette;
    BuildSkinningPalette(skeleton, model, palette);

    ASSERT_EQ(palette.size(), skeleton.Joints.size());
    for (const Mat4& entry : palette)
        ExpectNearIdentity(entry);
}

TEST(SkinningPalette, AChildInheritsItsParentsModelTransform)
{
    const SkeletonData skeleton = MakeChain();

    std::vector<Mat4> model;
    BuildBindModelTransforms(skeleton, model);

    // The tip's model position must differ from its local translation: if the
    // walk dropped the parent chain, they would coincide.
    const Vec4 tipOrigin = model[2] * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_FALSE(std::abs(tipOrigin.X - 0.25f) < 1e-4f
                 && std::abs(tipOrigin.Y - 1.0f) < 1e-4f);
}

TEST(SkinningPalette, APosedJointLeavesIdentityExactlyWhereItPoses)
{
    SkeletonData skeleton = MakeChain();

    std::vector<Mat4> model;
    BuildBindModelTransforms(skeleton, model);

    // Pose the mid joint away from bind: its palette entry and its child's
    // stop being identity; the root's stays.
    std::vector<Mat4> posed = model;
    posed[1] = posed[1] * Transform3f(Vec3d(0.0f, 0.5f, 0.0f),
                                      Quatf::FromAxisAngle(Vec3d::Up(), 0.3f),
                                      Vec3d::One()).ToMat4();
    posed[2] = posed[1] * model[0].Inverse() * model[2];

    std::vector<Mat4> palette;
    BuildSkinningPalette(skeleton, posed, palette);

    ExpectNearIdentity(palette[0]);
    EXPECT_GT(std::abs(palette[1][1][3] - 0.0f), 1e-3f);
}
