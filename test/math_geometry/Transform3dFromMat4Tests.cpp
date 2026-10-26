#include <gtest/gtest.h>

#include <math/Angle.h>
#include <math/geometry/3d/Transform3d.h>

TEST(Transform3dFromMat4, RecoversTheTransformAMatrixWasMadeFrom)
{
    const Transform3f original(Vec3d(1.0f, -2.0f, 3.5f),
                               Quat<float>::FromPitchYawRoll(ToRadians(20.0f), ToRadians(-65.0f), ToRadians(10.0f)),
                               Vec3d(1.0f, 2.0f, 0.5f));
    const Transform3f recovered = Transform3f::FromMat4(original.ToMat4());
    EXPECT_TRUE(recovered.NearlyEquals(original, 1e-5f));
}

TEST(Transform3dFromMat4, AComposedChainDecomposesToItsComposition)
{
    const Transform3f parent(Vec3d(0.0f, 1.0f, 0.0f), Quat<float>::FromAxisAngle(Vec3d(0.0f, 0.0f, 1.0f), 0.7f),
                             Vec3d::One());
    const Transform3f child(Vec3d(2.0f, 0.0f, 0.0f), Quat<float>::FromAxisAngle(Vec3d(1.0f, 0.0f, 0.0f), -0.3f),
                            Vec3d::One());
    const Transform3f composed = Transform3f::FromMat4(parent.ToMat4() * child.ToMat4());
    EXPECT_TRUE(composed.NearlyEquals(parent * child, 1e-5f));
}
