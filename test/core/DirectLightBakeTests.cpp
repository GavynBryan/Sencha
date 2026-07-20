#include <gtest/gtest.h>

#ifdef SENCHA_ENABLE_COOK

#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include <assets/cook/BakeBvh.h>
#include <assets/cook/DirectLightBake.h>
#include <math/Mat.h>

namespace
{
    // Mirror of the shader's RGBM decode (mesh_material.glsli).
    Vec3d DecodeBakedDirect(std::uint32_t packed)
    {
        const float r = ((packed >> 0) & 0xFFu) / 255.0f;
        const float g = ((packed >> 8) & 0xFFu) / 255.0f;
        const float b = ((packed >> 16) & 0xFFu) / 255.0f;
        const float a = ((packed >> 24) & 0xFFu) / 255.0f;
        return Vec3d(r, g, b) * (a * kBakedDirectRange);
    }

    // A quad (two triangles) centered at `center`, in the XZ plane (normal +Y),
    // spanning +/- half on X and Z. A blocker between a sample and a light
    // above it.
    void AddQuadXZ(std::vector<BakeTriangle>& tris, const Vec3d& center, float half)
    {
        const Vec3d a(center.X - half, center.Y, center.Z - half);
        const Vec3d b(center.X + half, center.Y, center.Z - half);
        const Vec3d c(center.X + half, center.Y, center.Z + half);
        const Vec3d d(center.X - half, center.Y, center.Z + half);
        tris.push_back({ a, b, c });
        tris.push_back({ a, c, d });
    }
}

TEST(BakedDirectRgbm, ZeroRadiancePacksToNeutral)
{
    EXPECT_EQ(EncodeBakedDirectRgbm(Vec3d(0.0f, 0.0f, 0.0f)), 0u);
}

TEST(BakedDirectRgbm, RoundTripsWithinQuantization)
{
    const Vec3d radiance(1.5f, 0.75f, 0.25f);
    const Vec3d decoded = DecodeBakedDirect(EncodeBakedDirectRgbm(radiance));
    EXPECT_NEAR(decoded.X, radiance.X, 0.05f);
    EXPECT_NEAR(decoded.Y, radiance.Y, 0.05f);
    EXPECT_NEAR(decoded.Z, radiance.Z, 0.05f);
}

TEST(DirectLightBake, UnoccludedSampleMatchesAnalyticModel)
{
    BakeDirectLight light{};
    light.Kind = BakeLightKind::Point;
    light.Position = Vec3d(0.0f, 2.0f, 0.0f);   // straight above, distance 2
    light.Color = Vec3d(1.0f, 1.0f, 1.0f);
    light.Intensity = 10.0f;
    light.Range = 5.0f;

    BakeBvh empty;   // no occluders
    const std::array<BakeDirectLight, 1> lights{ light };
    const Vec3d radiance = EvaluateBakedDirectRadiance(
        Vec3d(0.0f, 0.0f, 0.0f), Vec3d(0.0f, 1.0f, 0.0f), lights, empty,
        DirectLightBakeParams{});

    // Analytic: N.L = 1, wrap 0.25 -> diffuse = 1. window = 1-(2/5)^4, atten =
    // window^2 / (4 + 1e-4). radiance = 10 * atten.
    const float wrap = 0.25f;
    const float diffuse = (1.0f + wrap) / (1.0f + wrap);
    const float window = 1.0f - std::pow(2.0f / 5.0f, 4.0f);
    const float atten = (window * window) / (4.0f + 1e-4f);
    const float expected = 10.0f * atten * diffuse;

    EXPECT_NEAR(radiance.X, expected, 0.03f);
    EXPECT_NEAR(radiance.Y, expected, 0.03f);
    EXPECT_NEAR(radiance.Z, expected, 0.03f);
}

TEST(DirectLightBake, OccludedSampleReceivesNothing)
{
    BakeDirectLight light{};
    light.Position = Vec3d(0.0f, 2.0f, 0.0f);
    light.Intensity = 10.0f;
    light.Range = 5.0f;

    // A blocking quad halfway between the sample and the light.
    std::vector<BakeTriangle> tris;
    AddQuadXZ(tris, Vec3d(0.0f, 1.0f, 0.0f), 1.0f);
    BakeBvh bvh;
    bvh.Build(std::move(tris));

    const std::array<BakeDirectLight, 1> lights{ light };
    const Vec3d radiance = EvaluateBakedDirectRadiance(
        Vec3d(0.0f, 0.0f, 0.0f), Vec3d(0.0f, 1.0f, 0.0f), lights, bvh,
        DirectLightBakeParams{});
    EXPECT_EQ(EncodeBakedDirectRgbm(radiance), 0u);
}

TEST(DirectLightBake, BeyondRangeReceivesNothing)
{
    BakeDirectLight light{};
    light.Position = Vec3d(0.0f, 10.0f, 0.0f);   // distance 10 > range 3
    light.Intensity = 10.0f;
    light.Range = 3.0f;

    BakeBvh empty;
    const std::array<BakeDirectLight, 1> lights{ light };
    const Vec3d radiance = EvaluateBakedDirectRadiance(
        Vec3d(0.0f, 0.0f, 0.0f), Vec3d(0.0f, 1.0f, 0.0f), lights, empty,
        DirectLightBakeParams{});
    EXPECT_EQ(EncodeBakedDirectRgbm(radiance), 0u);
}

TEST(DirectLightBake, IsDeterministic)
{
    BakeDirectLight light{};
    light.Position = Vec3d(1.0f, 1.5f, 0.0f);
    light.Intensity = 8.0f;
    light.Range = 6.0f;
    const std::array<BakeDirectLight, 1> lights{ light };

    std::vector<BakeTriangle> tris;
    AddQuadXZ(tris, Vec3d(2.0f, 0.75f, 0.0f), 0.5f);
    BakeBvh bvh;
    bvh.Build(std::move(tris));

    for (int i = 0; i < 16; ++i)
    {
        const Vec3d position(static_cast<float>(i) * 0.5f, 0.0f, 0.0f);
        const Vec3d a = EvaluateBakedDirectRadiance(
            position, Vec3d(0.0f, 1.0f, 0.0f), lights, bvh, DirectLightBakeParams{});
        const Vec3d b = EvaluateBakedDirectRadiance(
            position, Vec3d(0.0f, 1.0f, 0.0f), lights, bvh, DirectLightBakeParams{});
        EXPECT_EQ(EncodeBakedDirectRgbm(a), EncodeBakedDirectRgbm(b));
    }
}

TEST(BakeBvh, SegmentOcclusionDetectsAndMissesBlocker)
{
    std::vector<BakeTriangle> tris;
    AddQuadXZ(tris, Vec3d(0.0f, 1.0f, 0.0f), 1.0f);
    BakeBvh bvh;
    bvh.Build(std::move(tris));

    // Straight through the quad.
    EXPECT_TRUE(bvh.SegmentOccluded(Vec3d(0.0f, 0.0f, 0.0f), Vec3d(0.0f, 2.0f, 0.0f)));
    // Beside the quad (x = 3 is outside the +/-1 span).
    EXPECT_FALSE(bvh.SegmentOccluded(Vec3d(3.0f, 0.0f, 0.0f), Vec3d(3.0f, 2.0f, 0.0f)));
}

TEST(BakeBvh, FirstHitBackfaceDistinguishesBuriedFromShadowed)
{
    // AddQuadXZ winds facing -Y (its front side is below). A sample below it
    // looking up sees the FRONT face (a valid sample under a ceiling); a
    // sample above it looking down sees the BACK face (buried behind the
    // surface); a sample above it looking up hits nothing (open sky).
    std::vector<BakeTriangle> tris;
    AddQuadXZ(tris, Vec3d(0.0f, 1.0f, 0.0f), 1.0f);
    BakeBvh bvh;
    bvh.Build(std::move(tris));

    EXPECT_FALSE(bvh.FirstHitIsBackface(
        Vec3d(0.0f, 0.0f, 0.0f), Vec3d(0.0f, 1.0f, 0.0f), 100.0));
    EXPECT_FALSE(bvh.FirstHitIsBackface(
        Vec3d(0.0f, 1.5f, 0.0f), Vec3d(0.0f, 1.0f, 0.0f), 100.0));
    EXPECT_TRUE(bvh.FirstHitIsBackface(
        Vec3d(0.0f, 2.0f, 0.0f), Vec3d(0.0f, -1.0f, 0.0f), 100.0));
}

TEST(BakeBvh, CoincidentFlushFacesNeverReportBackfaceFirst)
{
    // Two brushes kissing produce coincident triangles with opposite
    // windings at the shared interface. A ray crossing that interface from
    // open space hits both at the same t; the front face must win the tie in
    // EITHER build order, or whole lightmap regions classify as buried by
    // traversal-order luck (seen as black holes in the Benchmark bake).
    std::vector<BakeTriangle> frontFirst;
    AddQuadXZ(frontFirst, Vec3d(0.0f, 1.0f, 0.0f), 1.0f);  // front faces -Y
    {
        // The same quad flipped: front faces +Y.
        std::vector<BakeTriangle> flipped;
        AddQuadXZ(flipped, Vec3d(0.0f, 1.0f, 0.0f), 1.0f);
        for (BakeTriangle& tri : flipped)
            std::swap(tri.V1, tri.V2);
        frontFirst.insert(frontFirst.end(), flipped.begin(), flipped.end());
    }
    std::vector<BakeTriangle> backFirst(frontFirst.rbegin(), frontFirst.rend());

    BakeBvh a;
    a.Build(std::move(frontFirst));
    BakeBvh b;
    b.Build(std::move(backFirst));

    // From below looking up and from above looking down, one member of the
    // pair always faces the ray: never buried, whichever order built the BVH.
    for (const BakeBvh* bvh : { &a, &b })
    {
        EXPECT_FALSE(bvh->FirstHitIsBackface(
            Vec3d(0.2f, 0.0f, 0.1f), Vec3d(0.0f, 1.0f, 0.0f), 100.0));
        EXPECT_FALSE(bvh->FirstHitIsBackface(
            Vec3d(0.2f, 2.0f, 0.1f), Vec3d(0.0f, -1.0f, 0.0f), 100.0));
    }
}

#endif  // SENCHA_ENABLE_COOK
