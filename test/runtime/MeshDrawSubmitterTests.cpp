// Bind dedup across a run list, and the counters that describe what it saved.
//
// Two passes wrote this twice with different lifetimes -- the forward pass in
// locals scoped to one call, the shadow pass in members it had to remember to
// clear at every view. Both halves are decisions about values, so they are
// tested here without a device; only the vkCmd calls the delta asks for are
// left to a GPU run.

#include <gtest/gtest.h>

#include <graphics/vulkan/MeshDrawSubmitter.h>

namespace
{

VkPipeline Pipeline(std::uintptr_t id) { return reinterpret_cast<VkPipeline>(0x1000 + id); }
VkBuffer Buffer(std::uintptr_t id) { return reinterpret_cast<VkBuffer>(0x2000 + id); }

// A draw of `mesh`'s section, sized so triangle and instance counts are
// distinguishable in the tally.
MeshDrawCommand Draw(std::uintptr_t pipeline, std::uintptr_t mesh,
                     std::uint32_t indexCount = 30, std::uint32_t instances = 1)
{
    MeshDrawCommand draw{};
    draw.Pipeline = Pipeline(pipeline);
    draw.VertexBuffer = Buffer(mesh);
    draw.IndexBuffer = Buffer(mesh + 100);
    draw.IndexCount = indexCount;
    draw.InstanceCount = instances;
    return draw;
}

constexpr MeshBindingDelta kAll{ .Pipeline = true, .VertexBuffer = true, .IndexBuffer = true };
constexpr MeshBindingDelta kNone{};

} // namespace

TEST(MeshBindState, BindsEverythingForTheFirstDraw)
{
    MeshBindState state;
    EXPECT_EQ(state.Take(Draw(1, 1)), kAll);
}

TEST(MeshBindState, BindsNothingForAnIdenticalFollowingDraw)
{
    MeshBindState state;
    (void)state.Take(Draw(1, 1));
    EXPECT_EQ(state.Take(Draw(1, 1)), kNone);
}

TEST(MeshBindState, BindsOnlyThePipelineWhenOnlyTheMaterialChanges)
{
    // Two materials over one mesh: the common case the run merge cannot
    // collapse, and the one dedup is actually saving work on.
    MeshBindState state;
    (void)state.Take(Draw(1, 7));

    const MeshBindingDelta delta = state.Take(Draw(2, 7));
    EXPECT_EQ(delta, (MeshBindingDelta{ .Pipeline = true }));
}

TEST(MeshBindState, BindsOnlyTheBuffersWhenOnlyTheMeshChanges)
{
    MeshBindState state;
    (void)state.Take(Draw(1, 7));

    const MeshBindingDelta delta = state.Take(Draw(1, 8));
    EXPECT_EQ(delta, (MeshBindingDelta{ .VertexBuffer = true, .IndexBuffer = true }));
}

TEST(MeshBindState, RebindsEverythingAfterInvalidate)
{
    // The shadow pass rebinds its instance stream at the start of every view,
    // so what it recorded for the previous view no longer describes the
    // command buffer. Getting this wrong drops the first bind of each view.
    MeshBindState state;
    (void)state.Take(Draw(1, 1));
    EXPECT_EQ(state.Take(Draw(1, 1)), kNone);

    state.Invalidate();
    EXPECT_EQ(state.Take(Draw(1, 1)), kAll);
}

TEST(MeshBindState, RecordsTheDrawItWasAskedAboutEvenWithNothingToBind)
{
    // Take is not a query: it reports and records in one step, so a caller
    // cannot consult it twice and bind once.
    MeshBindState state;
    (void)state.Take(Draw(1, 1));
    EXPECT_EQ(state.Take(Draw(2, 2)), kAll);
    EXPECT_EQ(state.Take(Draw(2, 2)), kNone);
}

TEST(MeshDrawTally, SeparatesDrawCallsFromTheInstancesTheyCover)
{
    // The batching ratio. Counting one per draw on both sides makes them
    // always equal, which reports "nothing merged" no matter what merged.
    MeshDrawTally tally;
    tally.Add(Draw(1, 1, 30, 4), kAll);
    tally.Add(Draw(1, 2, 30, 6), MeshBindingDelta{ .VertexBuffer = true, .IndexBuffer = true });

    EXPECT_EQ(tally.Draws, 2u);
    EXPECT_EQ(tally.Instances, 10u);
}

TEST(MeshDrawTally, CountsTrianglesPerInstance)
{
    MeshDrawTally tally;
    tally.Add(Draw(1, 1, 30, 4), kAll);
    EXPECT_EQ(tally.Triangles, 40u);
}

TEST(MeshDrawTally, CountsOnlyTheBindsThatWereEmitted)
{
    MeshDrawTally tally;
    tally.Add(Draw(1, 1), kAll);
    tally.Add(Draw(2, 1), MeshBindingDelta{ .Pipeline = true });
    tally.Add(Draw(2, 1), kNone);

    EXPECT_EQ(tally.Draws, 3u);
    EXPECT_EQ(tally.PipelineBinds, 2u);
    EXPECT_EQ(tally.VertexBufferBinds, 1u);
    EXPECT_EQ(tally.IndexBufferBinds, 1u);
}

TEST(MeshDrawTally, StartsEmpty)
{
    const MeshDrawTally tally;
    EXPECT_EQ(tally.Draws, 0u);
    EXPECT_EQ(tally.Instances, 0u);
    EXPECT_EQ(tally.Triangles, 0u);
    EXPECT_EQ(tally.PipelineBinds, 0u);
}
