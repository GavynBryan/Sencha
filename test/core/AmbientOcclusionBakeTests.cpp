// The ambient-occlusion sample evaluator: cosine-weighted hemisphere
// visibility against the bake BVH. Open surfaces read 1, enclosure darkens,
// and occluders beyond the reach distance do not count.

#include <assets/cook/AmbientOcclusionBake.h>
#include <assets/cook/BakeBvh.h>
#include <assets/cook/ProbeBake.h>

#include <gtest/gtest.h>

#include <vector>

namespace
{
// Two triangles forming a y-plane quad spanning [-8, 8]^2 at `height`.
void AppendYPlaneQuad(std::vector<BakeTriangle>& out, float height)
{
    out.push_back({ Vec3d{ -8, height, -8 }, Vec3d{ 8, height, -8 },
                    Vec3d{ 8, height, 8 } });
    out.push_back({ Vec3d{ -8, height, -8 }, Vec3d{ 8, height, 8 },
                    Vec3d{ -8, height, 8 } });
}

float EvaluateAboveFloor(const std::vector<BakeTriangle>& extra,
                         float maxDistance)
{
    std::vector<BakeTriangle> triangles;
    AppendYPlaneQuad(triangles, 0.0f);
    triangles.insert(triangles.end(), extra.begin(), extra.end());
    BakeBvh bvh;
    bvh.Build(std::move(triangles));

    const std::vector<Vec3d> rayTable = BuildProbeRayTable(128);
    AmbientOcclusionBakeParams params;
    params.MaxDistance = maxDistance;
    params.RayTable = rayTable;
    return EvaluateAmbientOcclusion(Vec3d{ 0, 0, 0 }, Vec3d{ 0, 1, 0 }, bvh,
                                    params);
}
} // namespace

TEST(AmbientOcclusionBake, OpenFloorSampleIsFullyVisible)
{
    // Every hemisphere ray points away from the sample's own plane.
    EXPECT_FLOAT_EQ(EvaluateAboveFloor({}, 1.0f), 1.0f);
}

TEST(AmbientOcclusionBake, CeilingWithinReachOccludes)
{
    std::vector<BakeTriangle> ceiling;
    AppendYPlaneQuad(ceiling, 0.3f);
    const float ao = EvaluateAboveFloor(ceiling, 1.0f);
    EXPECT_LT(ao, 0.15f);
    EXPECT_GE(ao, 0.0f);
}

TEST(AmbientOcclusionBake, CeilingBeyondReachDoesNotCount)
{
    std::vector<BakeTriangle> ceiling;
    AppendYPlaneQuad(ceiling, 2.0f);
    EXPECT_FLOAT_EQ(EvaluateAboveFloor(ceiling, 1.0f), 1.0f);
}

TEST(AmbientOcclusionBake, WallBesideDarkensPartially)
{
    // An x-plane wall 0.2 to the sample's right blocks roughly the right
    // half of the hemisphere within reach.
    std::vector<BakeTriangle> wall;
    wall.push_back({ Vec3d{ 0.2f, 0, -8 }, Vec3d{ 0.2f, 8, -8 },
                     Vec3d{ 0.2f, 8, 8 } });
    wall.push_back({ Vec3d{ 0.2f, 0, -8 }, Vec3d{ 0.2f, 8, 8 },
                     Vec3d{ 0.2f, 0, 8 } });
    const float ao = EvaluateAboveFloor(wall, 1.0f);
    EXPECT_GT(ao, 0.3f);
    EXPECT_LT(ao, 0.9f);
}
