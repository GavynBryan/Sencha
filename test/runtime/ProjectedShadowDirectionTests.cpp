// Which way a grounding shadow falls, and how it gets there.
//
// The direction is an intensity-weighted blend over the lights affecting the
// caster plus a constant fallback floor, smoothed exponentially. The blend
// (not winner-take-all) is what makes two lights swapping dominance unable to
// pop the shadow; the fallback floor is what an unlit room grounds with; the
// explicit dt is what makes the smoothing a deterministic function of its
// inputs rather than of wall time.

#include <gtest/gtest.h>

#include <render/ProjectedShadowDirection.h>

#include <cmath>

namespace
{

GpuLight PointLight(Vec<3> position, float intensity, float range)
{
    GpuLight light;
    light.PositionRange = Vec4(position.X, position.Y, position.Z, range);
    light.ColorIntensity = Vec4(1.0f, 1.0f, 1.0f, intensity);
    light.Type = static_cast<std::uint32_t>(GpuLightType::Point);
    return light;
}

ProjectedShadowCaster CasterAt(Vec<3> center, std::uint32_t entityIndex)
{
    ProjectedShadowCaster caster;
    caster.Key = RenderEntityKey{ .Entity = EntityId{ entityIndex, 1 } };
    caster.WorldBounds =
        Aabb3d::FromCenterHalfExtent(center, Vec3d(0.5f, 1.0f, 0.5f));
    return caster;
}

float Dot(const Vec<3>& a, const Vec<3>& b) { return a.Dot(b); }

} // namespace

TEST(ProjectedShadowDirection, AStrongNearbyLightOwnsTheDirection)
{
    const GpuLight lights[] = { PointLight(Vec<3>(0.0f, 5.0f, 0.0f), 30.0f, 20.0f) };
    ProjectedShadowDirectionParams params;

    const Vec<3> direction = ProjectedShadowTargetDirection(
        lights, Vec<3>(2.0f, 1.0f, 0.0f), params);

    // Light above and left of the caster: the shadow falls down and away.
    EXPECT_GT(direction.X, 0.1f);
    EXPECT_LT(direction.Y, -0.5f);
}

TEST(ProjectedShadowDirection, NoLightMeansTheFallback)
{
    ProjectedShadowDirectionParams params;
    params.FallbackDirection = Vec<3>(0.0f, -1.0f, 0.0f);

    const Vec<3> direction = ProjectedShadowTargetDirection(
        {}, Vec<3>(0.0f, 1.0f, 0.0f), params);

    EXPECT_NEAR(direction.Y, -1.0f, 1e-5f);
}

TEST(ProjectedShadowDirection, AnOutOfRangeLightLeavesTheFallbackInCharge)
{
    const GpuLight lights[] = { PointLight(Vec<3>(100.0f, 5.0f, 0.0f), 30.0f, 10.0f) };
    ProjectedShadowDirectionParams params;
    params.FallbackDirection = Vec<3>(0.0f, -1.0f, 0.0f);

    const Vec<3> direction = ProjectedShadowTargetDirection(
        lights, Vec<3>(0.0f, 1.0f, 0.0f), params);

    // The (d/r)^4 window has fully closed, so the caster is not lit by it and
    // must not ground away from it.
    EXPECT_NEAR(direction.Y, -1.0f, 1e-4f);
}

TEST(ProjectedShadowDirection, TwoNearEqualLightsBlendInsteadOfPicking)
{
    // Two lights either side, one marginally stronger. Winner-take-all would
    // point the shadow fully away from the stronger; the blend must land
    // between, dominated by neither.
    const GpuLight lights[] = {
        PointLight(Vec<3>(-3.0f, 4.0f, 0.0f), 10.0f, 30.0f),
        PointLight(Vec<3>(3.0f, 4.0f, 0.0f), 10.5f, 30.0f),
    };
    ProjectedShadowDirectionParams params;

    const Vec<3> direction = ProjectedShadowTargetDirection(
        lights, Vec<3>(0.0f, 1.0f, 0.0f), params);

    // Mostly downward, only slightly biased toward the weaker light's side.
    EXPECT_LT(std::abs(direction.X), 0.35f);
    EXPECT_LT(direction.Y, -0.8f);
}

TEST(ProjectedShadowDirection, SmoothingConvergesAndNeverJumps)
{
    const GpuLight before[] = { PointLight(Vec<3>(-5.0f, 5.0f, 0.0f), 40.0f, 30.0f) };
    const GpuLight after[] = { PointLight(Vec<3>(5.0f, 5.0f, 0.0f), 40.0f, 30.0f) };
    ProjectedShadowDirectionParams params;
    params.SmoothingRate = 8.0f;

    ProjectedShadowSet set;
    set.Casters.push_back(CasterAt(Vec<3>(0.0f, 1.0f, 0.0f), 7));
    std::vector<ProjectedShadowDirectionState> state;

    // Settle under the first light.
    for (int frame = 0; frame < 60; ++frame)
        UpdateProjectedShadowDirections(set, before, state, 1.0f / 60.0f, params);
    const Vec<3> settled = set.Casters[0].Direction;
    EXPECT_GT(settled.X, 0.3f); // shadow points away from the light at -X

    // The light teleports to the other side: the direction must move toward
    // the new target every frame without ever jumping there in one.
    Vec<3> previous = settled;
    for (int frame = 0; frame < 120; ++frame)
    {
        UpdateProjectedShadowDirections(set, after, state, 1.0f / 60.0f, params);
        const Vec<3> current = set.Casters[0].Direction;
        EXPECT_GT(Dot(current, previous), 0.9f)
            << "direction moved more than ~25 degrees in one 60Hz frame";
        previous = current;
    }
    EXPECT_LT(previous.X, -0.3f); // arrived at the mirrored target
}

TEST(ProjectedShadowDirection, IdenticalInputSequencesProduceIdenticalDirections)
{
    const GpuLight lights[] = {
        PointLight(Vec<3>(1.0f, 6.0f, 2.0f), 25.0f, 25.0f),
        PointLight(Vec<3>(-4.0f, 3.0f, -1.0f), 15.0f, 25.0f),
    };
    ProjectedShadowDirectionParams params;

    const auto run = [&]
    {
        ProjectedShadowSet set;
        set.Casters.push_back(CasterAt(Vec<3>(0.0f, 1.0f, 0.0f), 3));
        set.Casters.push_back(CasterAt(Vec<3>(2.0f, 1.0f, 1.0f), 9));
        std::vector<ProjectedShadowDirectionState> state;
        for (int frame = 0; frame < 30; ++frame)
            UpdateProjectedShadowDirections(set, lights, state, 1.0f / 60.0f, params);
        return set;
    };

    const ProjectedShadowSet first = run();
    const ProjectedShadowSet second = run();
    for (std::size_t i = 0; i < first.Casters.size(); ++i)
    {
        EXPECT_EQ(first.Casters[i].Direction.X, second.Casters[i].Direction.X);
        EXPECT_EQ(first.Casters[i].Direction.Y, second.Casters[i].Direction.Y);
        EXPECT_EQ(first.Casters[i].Direction.Z, second.Casters[i].Direction.Z);
    }
}

TEST(ProjectedShadowDirection, StateForAVanishedCasterAgesOutAndOnlyThatOne)
{
    ProjectedShadowDirectionParams params;
    params.EvictAfterFrames = 5;

    ProjectedShadowSet both;
    both.Casters.push_back(CasterAt(Vec<3>(0.0f, 1.0f, 0.0f), 1));
    both.Casters.push_back(CasterAt(Vec<3>(4.0f, 1.0f, 0.0f), 2));
    std::vector<ProjectedShadowDirectionState> state;
    UpdateProjectedShadowDirections(both, {}, state, 1.0f / 60.0f, params);
    ASSERT_EQ(state.size(), 2u);

    ProjectedShadowSet onlyFirst;
    onlyFirst.Casters.push_back(CasterAt(Vec<3>(0.0f, 1.0f, 0.0f), 1));
    for (int frame = 0; frame < 10; ++frame)
        UpdateProjectedShadowDirections(onlyFirst, {}, state, 1.0f / 60.0f, params);

    ASSERT_EQ(state.size(), 1u);
    EXPECT_EQ(state[0].Key.Entity.Index, 1u);
}
