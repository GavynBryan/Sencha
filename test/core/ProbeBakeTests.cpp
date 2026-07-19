#include <assets/cook/BakeBvh.h>
#include <assets/cook/DirectLightBake.h>
#include <assets/cook/ProbeBake.h>
#include <jobs/ThreadPoolJobSystem.h>
#include <math/spatial/GridTransform3d.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <numbers>
#include <vector>

namespace
{
    // An axis-aligned box as 12 triangles. Outward winding (counter-clockwise
    // seen from outside) when facesOut; inward when not, which makes a sealed
    // room whose interior sees front faces.
    void AppendBox(std::vector<BakeTriangle>& out, const Vec3d& min,
                   const Vec3d& max, bool facesOut)
    {
        const Vec3d corners[8] = {
            { min.X, min.Y, min.Z }, { max.X, min.Y, min.Z },
            { min.X, max.Y, min.Z }, { max.X, max.Y, min.Z },
            { min.X, min.Y, max.Z }, { max.X, min.Y, max.Z },
            { min.X, max.Y, max.Z }, { max.X, max.Y, max.Z },
        };
        // Quads with outward counter-clockwise winding.
        const int quads[6][4] = {
            { 0, 2, 3, 1 },  // -Z
            { 4, 5, 7, 6 },  // +Z
            { 0, 4, 6, 2 },  // -X
            { 1, 3, 7, 5 },  // +X
            { 0, 1, 5, 4 },  // -Y
            { 2, 6, 7, 3 },  // +Y
        };
        for (const auto& quad : quads)
        {
            int a = quad[0], b = quad[1], c = quad[2], d = quad[3];
            if (!facesOut)
                std::swap(b, d);
            out.push_back({ corners[a], corners[b], corners[c] });
            out.push_back({ corners[a], corners[c], corners[d] });
        }
    }

    bool ShBitIdentical(const ProbeShL1& a, const ProbeShL1& b)
    {
        return std::memcmp(&a, &b, sizeof(ProbeShL1)) == 0;
    }
}

TEST(ProbeBake, RayTableIsUnitLengthDeterministicAndBalanced)
{
    const std::vector<Vec3d> table = BuildProbeRayTable(128);
    ASSERT_EQ(table.size(), 128u);

    Vec3d sum{};
    for (const Vec3d& d : table)
    {
        EXPECT_NEAR(d.Magnitude(), 1.0f, 1e-5f);
        sum += d;
    }
    // A Fibonacci spiral is nearly balanced; the mean direction is near zero.
    EXPECT_LT(sum.Magnitude() / 128.0f, 0.02f);

    const std::vector<Vec3d> again = BuildProbeRayTable(128);
    EXPECT_EQ(0, std::memcmp(table.data(), again.data(),
                             table.size() * sizeof(Vec3d)));
}

TEST(ProbeBake, ConstantEnvironmentProjectsToAnalyticSh)
{
    // Sky == ground == C makes the miss radiance a constant C over the whole
    // sphere. c0 is then exactly 4 pi C Y00 (the Y00 sum has no variance), and
    // the linear coefficients cancel to near zero.
    const std::vector<Vec3d> table = BuildProbeRayTable(128);
    BakeBvh empty;
    empty.Build({});

    ProbeBakeParams params;
    params.SkyColor = Vec3d(0.5f, 0.5f, 0.5f);
    params.GroundColor = Vec3d(0.5f, 0.5f, 0.5f);

    GridTransform3d grid;
    grid.DimsX = grid.DimsY = grid.DimsZ = 1;

    const ProbeVolumeBakeResult result =
        BakeProbeVolume(grid, empty, {}, table, params);
    ASSERT_EQ(result.Sh.size(), 1u);

    const float expectedC0 =
        4.0f * std::numbers::pi_v<float> * 0.5f * 0.2820948f;
    EXPECT_NEAR(result.Sh[0].R.W, expectedC0, 1e-3f);
    EXPECT_NEAR(result.Sh[0].R.X, 0.0f, 0.02f);
    EXPECT_NEAR(result.Sh[0].R.Y, 0.0f, 0.02f);
    EXPECT_NEAR(result.Sh[0].R.Z, 0.0f, 0.02f);

    // Cosine-convolved evaluation of a constant environment returns it.
    const Vec3d ambient = EvaluateProbeShL1(result.Sh[0], Vec3d(0.0f, 1.0f, 0.0f));
    EXPECT_NEAR(ambient.X, 0.5f, 0.01f);
    EXPECT_NEAR(ambient.Y, 0.5f, 0.01f);
    EXPECT_NEAR(ambient.Z, 0.5f, 0.01f);
}

TEST(ProbeBake, HemisphericEnvironmentTiltsTowardSky)
{
    const std::vector<Vec3d> table = BuildProbeRayTable(128);
    BakeBvh empty;
    empty.Build({});

    ProbeBakeParams params;
    params.SkyColor = Vec3d(1.0f, 1.0f, 1.0f);
    params.GroundColor = Vec3d(0.0f, 0.0f, 0.0f);

    GridTransform3d grid;
    grid.DimsX = grid.DimsY = grid.DimsZ = 1;

    const ProbeVolumeBakeResult result =
        BakeProbeVolume(grid, empty, {}, table, params);

    const Vec3d up = EvaluateProbeShL1(result.Sh[0], Vec3d(0.0f, 1.0f, 0.0f));
    const Vec3d down = EvaluateProbeShL1(result.Sh[0], Vec3d(0.0f, -1.0f, 0.0f));
    EXPECT_GT(up.X, down.X);

    // The traced result matches the analytic projection of the same
    // environment within Monte Carlo tolerance.
    const ProbeShL1 analytic =
        ProjectHemisphericAmbientSh(params.SkyColor, params.GroundColor);
    EXPECT_NEAR(result.Sh[0].R.W, analytic.R.W, 0.02f);
    EXPECT_NEAR(result.Sh[0].R.Y, analytic.R.Y, 0.05f);
}

TEST(ProbeBake, AnalyticAmbientEvaluatesToConvolvedHemisphere)
{
    const ProbeShL1 sh =
        ProjectHemisphericAmbientSh(Vec3d(1.0f, 1.0f, 1.0f), Vec3d(0.0f, 0.0f, 0.0f));
    // For L(d) = 0.5 + 0.5 d.y the cosine-convolved response at the pole is
    // mean + (2/3) halfDiff: softer than the raw hemi factor by design.
    const Vec3d up = EvaluateProbeShL1(sh, Vec3d(0.0f, 1.0f, 0.0f));
    EXPECT_NEAR(up.X, 0.5f + (2.0f / 3.0f) * 0.5f, 1e-3f);
    const Vec3d down = EvaluateProbeShL1(sh, Vec3d(0.0f, -1.0f, 0.0f));
    EXPECT_NEAR(down.X, 0.5f - (2.0f / 3.0f) * 0.5f, 1e-3f);
}

TEST(ProbeBake, BuriedProbeIsInvalidatedAndDilatedFromNeighbors)
{
    // A solid box occupies the middle of a 3-probe row; the middle probe sits
    // inside it (all short rays hit back faces) and must take its neighbors'
    // average instead of baking black.
    std::vector<BakeTriangle> triangles;
    AppendBox(triangles, Vec3d(0.6f, -1.0f, -1.0f), Vec3d(1.4f, 1.0f, 1.0f), true);
    BakeBvh bvh;
    bvh.Build(std::move(triangles));

    const std::vector<Vec3d> table = BuildProbeRayTable(64);
    ProbeBakeParams params;
    params.SkyColor = Vec3d(1.0f, 1.0f, 1.0f);
    params.GroundColor = Vec3d(1.0f, 1.0f, 1.0f);
    // Far enough that every short ray from inside the box reaches a back face
    // (the box's farthest interior corner is ~1.5 away from its center).
    params.ClassifyRayDistance = 2.0f;

    GridTransform3d grid;
    grid.Origin = Vec3d(0.0f, 0.0f, 0.0f);
    grid.CellSize = 1.0f;
    grid.DimsX = 3;
    grid.DimsY = 1;
    grid.DimsZ = 1;

    const ProbeVolumeBakeResult result =
        BakeProbeVolume(grid, bvh, {}, table, params);
    ASSERT_EQ(result.Sh.size(), 3u);
    EXPECT_EQ(result.Valid[0], 1);
    EXPECT_EQ(result.Valid[1], 0);
    EXPECT_EQ(result.Valid[2], 1);
    EXPECT_EQ(result.InvalidCount, 1u);
    EXPECT_EQ(result.UnfilledCount, 0u);

    // The dilated probe is the mean of its two valid neighbors.
    const float expected = 0.5f * (result.Sh[0].R.W + result.Sh[2].R.W);
    EXPECT_NEAR(result.Sh[1].R.W, expected, 1e-5f);
    EXPECT_GT(result.Sh[1].R.W, 0.0f);
}

TEST(ProbeBake, FullyEnclosedUnfilledProbesTakeTheHemisphericAmbient)
{
    // Every probe buried: nothing to dilate from, so the ambient fallback
    // fills the volume instead of leaving black.
    std::vector<BakeTriangle> triangles;
    AppendBox(triangles, Vec3d(-2.0f, -2.0f, -2.0f), Vec3d(2.0f, 2.0f, 2.0f), true);
    BakeBvh bvh;
    bvh.Build(std::move(triangles));

    const std::vector<Vec3d> table = BuildProbeRayTable(64);
    ProbeBakeParams params;
    params.SkyColor = Vec3d(0.25f, 0.5f, 1.0f);
    params.GroundColor = Vec3d(0.1f, 0.1f, 0.1f);
    params.ClassifyRayDistance = 10.0f;

    GridTransform3d grid;
    grid.Origin = Vec3d(-0.5f, -0.5f, -0.5f);
    grid.CellSize = 1.0f;
    grid.DimsX = 2;
    grid.DimsY = 2;
    grid.DimsZ = 2;

    const ProbeVolumeBakeResult result =
        BakeProbeVolume(grid, bvh, {}, table, params);
    EXPECT_EQ(result.InvalidCount, 8u);
    EXPECT_EQ(result.UnfilledCount, 8u);

    const ProbeShL1 ambient =
        ProjectHemisphericAmbientSh(params.SkyColor, params.GroundColor);
    for (const ProbeShL1& sh : result.Sh)
        EXPECT_TRUE(ShBitIdentical(sh, ambient));
}

TEST(ProbeBake, SerialAndParallelBakesAreBitIdentical)
{
    std::vector<BakeTriangle> triangles;
    AppendBox(triangles, Vec3d(-4.0f, -1.0f, -4.0f), Vec3d(4.0f, 3.0f, 4.0f), false);
    AppendBox(triangles, Vec3d(-1.0f, -1.0f, -1.0f), Vec3d(1.0f, 1.0f, 1.0f), true);
    BakeBvh bvh;
    bvh.Build(std::move(triangles));

    std::vector<BakeDirectLight> lights(1);
    lights[0].Position = Vec3d(2.0f, 2.0f, 2.0f);
    lights[0].Intensity = 10.0f;
    lights[0].Range = 20.0f;

    const std::vector<Vec3d> table = BuildProbeRayTable(64);
    ProbeBakeParams params;
    params.SkyColor = Vec3d(0.3f, 0.4f, 0.5f);
    params.GroundColor = Vec3d(0.2f, 0.15f, 0.1f);

    GridTransform3d grid;
    grid.Origin = Vec3d(-3.0f, 0.0f, -3.0f);
    grid.CellSize = 1.5f;
    grid.DimsX = 5;
    grid.DimsY = 3;
    grid.DimsZ = 5;

    const ProbeVolumeBakeResult serial =
        BakeProbeVolume(grid, bvh, lights, table, params, nullptr);

    ThreadPoolJobSystem parallel(4);
    const ProbeVolumeBakeResult threaded =
        BakeProbeVolume(grid, bvh, lights, table, params, &parallel);

    ASSERT_EQ(serial.Sh.size(), threaded.Sh.size());
    EXPECT_EQ(0, std::memcmp(serial.Sh.data(), threaded.Sh.data(),
                             serial.Sh.size() * sizeof(ProbeShL1)));
    EXPECT_EQ(serial.Valid, threaded.Valid);
    EXPECT_EQ(serial.InvalidCount, threaded.InvalidCount);
    EXPECT_EQ(serial.UnfilledCount, threaded.UnfilledCount);
}

TEST(ProbeBake, HaloAssemblyFiltersByReachAndOrdersByContentHash)
{
    std::vector<BakeTriangle> own;
    own.push_back({ Vec3d(0, 0, 0), Vec3d(1, 0, 0), Vec3d(0, 1, 0) });

    ProbeHaloZone near1;
    near1.ContentHash = 0xBBBB;
    near1.Bounds = Aabb3d{ Vec3d(11, 0, 0), Vec3d(13, 2, 2) };
    near1.Triangles.push_back({ Vec3d(11, 0, 0), Vec3d(12, 0, 0), Vec3d(11, 1, 0) });

    ProbeHaloZone near2;
    near2.ContentHash = 0xAAAA;
    near2.Bounds = Aabb3d{ Vec3d(-13, 0, 0), Vec3d(-11, 2, 2) };
    near2.Triangles.push_back({ Vec3d(-12, 0, 0), Vec3d(-11, 0, 0), Vec3d(-12, 1, 0) });

    ProbeHaloZone far;
    far.ContentHash = 0x1111;
    far.Bounds = Aabb3d{ Vec3d(200, 0, 0), Vec3d(202, 2, 2) };
    far.Triangles.push_back({ Vec3d(200, 0, 0), Vec3d(201, 0, 0), Vec3d(200, 1, 0) });

    const Aabb3d zoneBounds{ Vec3d(0, 0, 0), Vec3d(10, 2, 10) };
    const std::vector<ProbeHaloZone> haloA = { near1, far, near2 };
    const std::vector<ProbeHaloZone> haloB = { near2, near1, far };

    const std::vector<BakeTriangle> a =
        AssembleProbeBakeTriangles(own, zoneBounds, haloA, 20.0f);
    const std::vector<BakeTriangle> b =
        AssembleProbeBakeTriangles(own, zoneBounds, haloB, 20.0f);

    // The out-of-reach zone is excluded; both input orders assemble the same
    // byte sequence because halo zones sort by content hash.
    ASSERT_EQ(a.size(), 3u);
    ASSERT_EQ(b.size(), 3u);
    EXPECT_EQ(0, std::memcmp(a.data(), b.data(), a.size() * sizeof(BakeTriangle)));
    EXPECT_FLOAT_EQ(a[1].V0.X, -12.0f);  // hash 0xAAAA before 0xBBBB
    EXPECT_FLOAT_EQ(a[2].V0.X, 11.0f);
}

TEST(ProbeBake, AdjacentZonesComputeIdenticalBoundarySamples)
{
    // Two floor slabs side by side, one zone each, a probe above the shared
    // edge. Baking that probe from zone A's assembly (own = A, halo = B) and
    // from zone B's (own = B, halo = A) must produce bit-identical SH: the
    // halo is what makes per-zone bakes seamless. Without it, each side would
    // see half a floor and disagree.
    const auto floorQuad = [](float x0, float x1)
    {
        // Normal +Y (front face seen from above).
        std::vector<BakeTriangle> tris;
        tris.push_back({ Vec3d(x0, 0, -4), Vec3d(x0, 0, 4), Vec3d(x1, 0, 4) });
        tris.push_back({ Vec3d(x0, 0, -4), Vec3d(x1, 0, 4), Vec3d(x1, 0, -4) });
        return tris;
    };
    const std::vector<BakeTriangle> floorA = floorQuad(-8.0f, 0.0f);
    const std::vector<BakeTriangle> floorB = floorQuad(0.0f, 8.0f);

    ProbeHaloZone haloOfB;
    haloOfB.ContentHash = 0x2222;
    haloOfB.Bounds = Aabb3d{ Vec3d(0.0f, 0.0f, -4.0f), Vec3d(8.0f, 0.0f, 4.0f) };
    haloOfB.Triangles = floorB;

    ProbeHaloZone haloOfA;
    haloOfA.ContentHash = 0x3333;
    haloOfA.Bounds = Aabb3d{ Vec3d(-8.0f, 0.0f, -4.0f), Vec3d(0.0f, 0.0f, 4.0f) };
    haloOfA.Triangles = floorA;

    const Aabb3d boundsA{ Vec3d(-8.0f, 0.0f, -4.0f), Vec3d(0.0f, 0.0f, 4.0f) };
    const Aabb3d boundsB{ Vec3d(0.0f, 0.0f, -4.0f), Vec3d(8.0f, 0.0f, 4.0f) };

    BakeBvh bvhA;
    bvhA.Build(AssembleProbeBakeTriangles(floorA, boundsA, { &haloOfB, 1 }, 100.0f));
    BakeBvh bvhB;
    bvhB.Build(AssembleProbeBakeTriangles(floorB, boundsB, { &haloOfA, 1 }, 100.0f));

    std::vector<BakeDirectLight> lights(1);
    lights[0].Position = Vec3d(-2.0f, 3.0f, 0.0f);
    lights[0].Intensity = 20.0f;
    lights[0].Range = 30.0f;

    const std::vector<Vec3d> table = BuildProbeRayTable(128);
    ProbeBakeParams params;
    params.SkyColor = Vec3d(0.2f, 0.3f, 0.4f);
    params.GroundColor = Vec3d(0.1f, 0.1f, 0.1f);

    // One probe above the shared boundary edge.
    GridTransform3d boundary;
    boundary.Origin = Vec3d(0.0f, 1.0f, 0.0f);
    boundary.DimsX = boundary.DimsY = boundary.DimsZ = 1;

    const ProbeVolumeBakeResult fromA =
        BakeProbeVolume(boundary, bvhA, lights, table, params);
    const ProbeVolumeBakeResult fromB =
        BakeProbeVolume(boundary, bvhB, lights, table, params);

    ASSERT_EQ(fromA.Valid[0], 1);
    ASSERT_EQ(fromB.Valid[0], 1);
    EXPECT_TRUE(ShBitIdentical(fromA.Sh[0], fromB.Sh[0]));

    // And the halo genuinely mattered: without zone B's floor the probe's
    // bounce differs (half the floor is missing).
    BakeBvh bvhAOnly;
    bvhAOnly.Build(floorA);
    const ProbeVolumeBakeResult withoutHalo =
        BakeProbeVolume(boundary, bvhAOnly, lights, table, params);
    EXPECT_FALSE(ShBitIdentical(fromA.Sh[0], withoutHalo.Sh[0]));
}

TEST(ProbeBake, TwoRoomBakeKeepsTheDarkRoomDark)
{
    // The 3B.1 completion criterion: two sealed rooms sharing a solid wall,
    // a light in one. Probes in the lit room see bounce; probes in the dark
    // room stay near black (no sky reaches a sealed interior).
    std::vector<BakeTriangle> triangles;
    AppendBox(triangles, Vec3d(-8.0f, 0.0f, -4.0f), Vec3d(0.0f, 4.0f, 4.0f), false);
    AppendBox(triangles, Vec3d(0.0f, 0.0f, -4.0f), Vec3d(8.0f, 4.0f, 4.0f), false);
    BakeBvh bvh;
    bvh.Build(std::move(triangles));

    std::vector<BakeDirectLight> lights(1);
    lights[0].Position = Vec3d(-4.0f, 2.0f, 0.0f);  // lit room: x < 0
    lights[0].Intensity = 30.0f;
    lights[0].Range = 30.0f;

    const std::vector<Vec3d> table = BuildProbeRayTable(128);
    ProbeBakeParams params;
    params.SkyColor = Vec3d(1.0f, 1.0f, 1.0f);  // never reached: rooms sealed
    params.GroundColor = Vec3d(1.0f, 1.0f, 1.0f);

    GridTransform3d grid;
    grid.Origin = Vec3d(-6.0f, 2.0f, 0.0f);
    grid.CellSize = 2.0f;
    grid.DimsX = 7;  // spans x in [-6, 6] through both rooms
    grid.DimsY = 1;
    grid.DimsZ = 1;

    const ProbeVolumeBakeResult baked =
        BakeProbeVolume(grid, bvh, lights, table, params);

    float litSum = 0.0f;
    float darkSum = 0.0f;
    for (std::uint32_t x = 0; x < grid.DimsX; ++x)
    {
        const Vec3d position = grid.PointPosition(x, 0, 0);
        if (baked.Valid[x] == 0)
            continue;  // a probe caught in the shared wall is dilated
        const Vec3d ambient =
            EvaluateProbeShL1(baked.Sh[x], Vec3d(0.0f, 1.0f, 0.0f));
        const float mean = (ambient.X + ambient.Y + ambient.Z) / 3.0f;
        if (position.X < -0.5f)
            litSum += mean;
        else if (position.X > 0.5f)
            darkSum += mean;
    }

    EXPECT_GT(litSum, 0.001f);
    EXPECT_LT(darkSum, litSum * 0.05f);
}
