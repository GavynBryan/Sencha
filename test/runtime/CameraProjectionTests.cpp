// Pins the renderer's clip-space convention.
//
// docs/renderer/vulkan-backend.md records standard [0,1] depth, not reversed Z,
// paired with VK_COMPARE_OP_LESS_OR_EQUAL and a 1.0 depth clear across every
// pipeline. Nothing enforced that: the projection was three copied functions
// and no test read the matrix they produced. Flipping the depth mapping here
// does not break a build, it silently inverts every depth test, so these
// assertions exist to make that a red test instead.
//
// If reverse-Z is ever adopted, these are the tests that must change first, and
// the compare ops and clear values must change with them.

#include <gtest/gtest.h>

#include <render/CameraProjection.h>

namespace
{

constexpr float kPi = 3.14159265358979323846f;

// Projects a view-space point and performs the perspective divide.
Vec4 ToClip(const Mat4& projection, float x, float y, float z)
{
    return projection * Vec4{ x, y, z, 1.0f };
}

float NdcDepth(const Mat4& projection, float viewZ)
{
    const Vec4 clip = ToClip(projection, 0.0f, 0.0f, viewZ);
    return clip.Z / clip.W;
}

} // namespace

// --- perspective ---

TEST(VulkanPerspective, MapsTheNearPlaneToZeroAndTheFarPlaneToOne)
{
    constexpr float nearPlane = 0.1f;
    constexpr float farPlane = 100.0f;
    const Mat4 projection =
        MakeVulkanPerspective(kPi / 4.0f, 16.0f / 9.0f, nearPlane, farPlane);

    // The camera looks down -Z, so the planes sit at negative view depths.
    EXPECT_NEAR(NdcDepth(projection, -nearPlane), 0.0f, 1e-5f);
    EXPECT_NEAR(NdcDepth(projection, -farPlane), 1.0f, 1e-5f);
}

TEST(VulkanPerspective, DepthIncreasesWithDistanceRatherThanReversing)
{
    const Mat4 projection = MakeVulkanPerspective(kPi / 4.0f, 1.0f, 0.1f, 100.0f);

    const float nearer = NdcDepth(projection, -1.0f);
    const float farther = NdcDepth(projection, -50.0f);
    EXPECT_LT(nearer, farther)
        << "reversed-Z would invert this, and every pipeline uses LESS_OR_EQUAL";
    EXPECT_GE(nearer, 0.0f);
    EXPECT_LE(farther, 1.0f);
}

TEST(VulkanPerspective, FlipsYForVulkanNdc)
{
    const Mat4 projection = MakeVulkanPerspective(kPi / 4.0f, 1.0f, 0.1f, 100.0f);

    // A point above the view axis must land in negative NDC Y: Vulkan's +Y
    // points down the framebuffer.
    const Vec4 clip = ToClip(projection, 0.0f, 1.0f, -2.0f);
    EXPECT_LT(clip.Y / clip.W, 0.0f);
}

TEST(VulkanPerspective, AspectRatioNarrowsXNotY)
{
    const Mat4 square = MakeVulkanPerspective(kPi / 4.0f, 1.0f, 0.1f, 100.0f);
    const Mat4 wide = MakeVulkanPerspective(kPi / 4.0f, 2.0f, 0.1f, 100.0f);

    // Widening the aspect must compress X and leave the vertical field alone,
    // which is what makes fovY the authored quantity.
    EXPECT_LT(wide[0][0], square[0][0]);
    EXPECT_FLOAT_EQ(wide[1][1], square[1][1]);
}

TEST(VulkanPerspective, WiderFieldOfViewCompressesBothAxes)
{
    const Mat4 narrow = MakeVulkanPerspective(kPi / 6.0f, 1.0f, 0.1f, 100.0f);
    const Mat4 wide = MakeVulkanPerspective(kPi / 3.0f, 1.0f, 0.1f, 100.0f);

    EXPECT_LT(wide[0][0], narrow[0][0]);
    EXPECT_GT(wide[1][1], narrow[1][1]);  // both negative; wider means closer to zero
}

TEST(VulkanPerspective, CarriesViewDepthIntoWForThePerspectiveDivide)
{
    const Mat4 projection = MakeVulkanPerspective(kPi / 4.0f, 1.0f, 0.1f, 100.0f);

    const Vec4 clip = ToClip(projection, 0.0f, 0.0f, -7.0f);
    EXPECT_FLOAT_EQ(clip.W, 7.0f);
}

// --- orthographic ---

TEST(VulkanOrthographic, MapsTheNearPlaneToZeroAndTheFarPlaneToOne)
{
    constexpr float nearPlane = 0.1f;
    constexpr float farPlane = 100.0f;
    const Mat4 projection = MakeVulkanOrthographic(
        -10.0f, 10.0f, -10.0f, 10.0f, nearPlane, farPlane);

    EXPECT_NEAR(NdcDepth(projection, -nearPlane), 0.0f, 1e-5f);
    EXPECT_NEAR(NdcDepth(projection, -farPlane), 1.0f, 1e-5f);
}

TEST(VulkanOrthographic, FlipsYForVulkanNdc)
{
    const Mat4 projection =
        MakeVulkanOrthographic(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 100.0f);

    const Vec4 clip = ToClip(projection, 0.0f, 5.0f, -1.0f);
    EXPECT_LT(clip.Y / clip.W, 0.0f);
}

TEST(VulkanOrthographic, MapsTheExtentsToTheNdcSquare)
{
    const Mat4 projection =
        MakeVulkanOrthographic(-4.0f, 4.0f, -3.0f, 3.0f, 0.1f, 100.0f);

    const Vec4 rightEdge = ToClip(projection, 4.0f, 0.0f, -1.0f);
    EXPECT_NEAR(rightEdge.X / rightEdge.W, 1.0f, 1e-5f);

    // Bottom in view space maps to +1 in NDC because of the Y flip.
    const Vec4 bottomEdge = ToClip(projection, 0.0f, -3.0f, -1.0f);
    EXPECT_NEAR(bottomEdge.Y / bottomEdge.W, 1.0f, 1e-5f);
}

TEST(VulkanOrthographic, HandlesAnOffCenterVolume)
{
    // Asymmetric bounds are what an ortho viewport produces when its panel is
    // not centred on the focus point.
    const Mat4 projection =
        MakeVulkanOrthographic(0.0f, 8.0f, 0.0f, 6.0f, 0.1f, 100.0f);

    const Vec4 centre = ToClip(projection, 4.0f, 3.0f, -1.0f);
    EXPECT_NEAR(centre.X / centre.W, 0.0f, 1e-5f);
    EXPECT_NEAR(centre.Y / centre.W, 0.0f, 1e-5f);
}

TEST(VulkanOrthographic, KeepsWAtOneSoDepthIsLinear)
{
    const Mat4 projection =
        MakeVulkanOrthographic(-10.0f, 10.0f, -10.0f, 10.0f, 0.0f, 100.0f);

    const Vec4 clip = ToClip(projection, 1.0f, 2.0f, -25.0f);
    EXPECT_FLOAT_EQ(clip.W, 1.0f);
    EXPECT_NEAR(clip.Z, 0.25f, 1e-5f);
}
