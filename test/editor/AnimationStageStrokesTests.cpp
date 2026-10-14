#include "authoring/AnimationStageStrokes.h"

#include <anim/Skeleton.h>

#include <gtest/gtest.h>

// What is drawn over a rig on its stage: a bone from every joint to its
// parent, where the posed joints are.
TEST(AnimationStageStrokes, ABoneRunsFromEachJointToItsParentWhereThePoseHasThem)
{
    SkeletonData skeleton;
    skeleton.Joints = { { .Name = "hips" }, { .Name = "spine", .ParentIndex = 0 }, { .Name = "head", .ParentIndex = 1 } };
    const std::vector<Mat4> model{ Mat4::MakeTranslation(0.0f, 1.0f, 0.0f), Mat4::MakeTranslation(0.0f, 1.5f, 0.0f),
                                   Mat4::MakeTranslation(0.2f, 1.8f, 0.0f) };
    std::vector<EditorLineSegment> strokes;
    AppendSkeletonStrokes(skeleton, model, strokes);
    ASSERT_EQ(strokes.size(), 2u) << "a root has no bone";
    EXPECT_EQ(strokes[0].A, Vec3d(0.0f, 1.0f, 0.0f));
    EXPECT_EQ(strokes[0].B, Vec3d(0.0f, 1.5f, 0.0f));
    EXPECT_EQ(strokes[1].B, Vec3d(0.2f, 1.8f, 0.0f));

    strokes.clear();
    AppendSkeletonStrokes(skeleton, std::span<const Mat4>(model).first(1), strokes);
    EXPECT_TRUE(strokes.empty()) << "a pose shorter than the skeleton draws only what it has";
}
