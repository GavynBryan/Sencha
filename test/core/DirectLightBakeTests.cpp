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
#include <render/static_mesh/MeshGeometry.h>

namespace
{
    // Mirror of the shader's RGBM decode (mesh_forward.vert.glsl).
    Vec3d DecodeBakedDirect(std::uint32_t packed)
    {
        const float r = ((packed >> 0) & 0xFFu) / 255.0f;
        const float g = ((packed >> 8) & 0xFFu) / 255.0f;
        const float b = ((packed >> 16) & 0xFFu) / 255.0f;
        const float a = ((packed >> 24) & 0xFFu) / 255.0f;
        return Vec3d(r, g, b) * (a * kBakedDirectRange);
    }

    StaticMeshVertex Vertex(const Vec3d& position, const Vec3d& normal)
    {
        StaticMeshVertex v{};
        v.Position = position;
        v.Normal = normal;
        return v;
    }

    // A quad (two triangles) centered at `center`, in the XZ plane (normal +Y),
    // spanning +/- half on X and Z. A blocker between a floor vertex and a
    // light above it.
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

TEST(DirectLightBake, UnoccludedVertexMatchesAnalyticModel)
{
    MeshGeometry geometry;
    geometry.Vertices = { Vertex(Vec3d(0.0f, 0.0f, 0.0f), Vec3d(0.0f, 1.0f, 0.0f)) };

    BakeDirectLight light{};
    light.Kind = BakeLightKind::Point;
    light.Position = Vec3d(0.0f, 2.0f, 0.0f);   // straight above, distance 2
    light.Color = Vec3d(1.0f, 1.0f, 1.0f);
    light.Intensity = 10.0f;
    light.Range = 5.0f;

    BakeBvh empty;   // no occluders
    const std::array<BakeDirectLight, 1> lights{ light };
    DirectLightBakeParams params{};
    ASSERT_TRUE(BakeDirectLighting(geometry, Mat4::Identity(), lights, empty, params));

    // Analytic: N.L = 1, wrap 0.25 -> diffuse = 1. window = 1-(2/5)^4, atten =
    // window^2 / (4 + 1e-4). radiance = 10 * atten.
    const float wrap = 0.25f;
    const float diffuse = (1.0f + wrap) / (1.0f + wrap);
    const float window = 1.0f - std::pow(2.0f / 5.0f, 4.0f);
    const float atten = (window * window) / (4.0f + 1e-4f);
    const float expected = 10.0f * atten * diffuse;

    const Vec3d decoded = DecodeBakedDirect(geometry.Vertices[0].BakedDirect);
    EXPECT_NEAR(decoded.X, expected, 0.03f);
    EXPECT_NEAR(decoded.Y, expected, 0.03f);
    EXPECT_NEAR(decoded.Z, expected, 0.03f);
}

TEST(DirectLightBake, OccludedVertexReceivesNothing)
{
    MeshGeometry geometry;
    geometry.Vertices = { Vertex(Vec3d(0.0f, 0.0f, 0.0f), Vec3d(0.0f, 1.0f, 0.0f)) };

    BakeDirectLight light{};
    light.Position = Vec3d(0.0f, 2.0f, 0.0f);
    light.Intensity = 10.0f;
    light.Range = 5.0f;

    // A blocking quad halfway between the vertex and the light.
    std::vector<BakeTriangle> tris;
    AddQuadXZ(tris, Vec3d(0.0f, 1.0f, 0.0f), 1.0f);
    BakeBvh bvh;
    bvh.Build(std::move(tris));

    const std::array<BakeDirectLight, 1> lights{ light };
    const bool anyLit =
        BakeDirectLighting(geometry, Mat4::Identity(), lights, bvh, DirectLightBakeParams{});

    EXPECT_FALSE(anyLit);
    EXPECT_EQ(geometry.Vertices[0].BakedDirect, 0u);
}

TEST(DirectLightBake, BeyondRangeReceivesNothing)
{
    MeshGeometry geometry;
    geometry.Vertices = { Vertex(Vec3d(0.0f, 0.0f, 0.0f), Vec3d(0.0f, 1.0f, 0.0f)) };

    BakeDirectLight light{};
    light.Position = Vec3d(0.0f, 10.0f, 0.0f);   // distance 10 > range 3
    light.Intensity = 10.0f;
    light.Range = 3.0f;

    BakeBvh empty;
    const std::array<BakeDirectLight, 1> lights{ light };
    EXPECT_FALSE(BakeDirectLighting(geometry, Mat4::Identity(), lights, empty,
                                    DirectLightBakeParams{}));
    EXPECT_EQ(geometry.Vertices[0].BakedDirect, 0u);
}

TEST(DirectLightBake, IsDeterministic)
{
    auto makeGeometry = [] {
        MeshGeometry g;
        for (int i = 0; i < 16; ++i)
            g.Vertices.push_back(Vertex(
                Vec3d(static_cast<float>(i) * 0.5f, 0.0f, 0.0f),
                Vec3d(0.0f, 1.0f, 0.0f)));
        return g;
    };

    BakeDirectLight light{};
    light.Position = Vec3d(1.0f, 1.5f, 0.0f);
    light.Intensity = 8.0f;
    light.Range = 6.0f;
    const std::array<BakeDirectLight, 1> lights{ light };

    std::vector<BakeTriangle> tris;
    AddQuadXZ(tris, Vec3d(2.0f, 0.75f, 0.0f), 0.5f);
    BakeBvh bvh;
    bvh.Build(std::move(tris));

    MeshGeometry a = makeGeometry();
    MeshGeometry b = makeGeometry();
    BakeDirectLighting(a, Mat4::Identity(), lights, bvh, DirectLightBakeParams{});
    BakeDirectLighting(b, Mat4::Identity(), lights, bvh, DirectLightBakeParams{});

    for (std::size_t i = 0; i < a.Vertices.size(); ++i)
        EXPECT_EQ(a.Vertices[i].BakedDirect, b.Vertices[i].BakedDirect);
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

#endif  // SENCHA_ENABLE_COOK
