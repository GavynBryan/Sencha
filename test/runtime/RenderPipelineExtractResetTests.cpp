#include <gtest/gtest.h>

#include <app/DefaultRenderPipeline.h>
#include <app/GameContexts.h>
#include <core/config/EngineConfig.h>
#include <ecs/World.h>
#include <ecs/StoragePartitionSet.h>
#include <runtime/RuntimeFrameLoop.h>

// A render frame the pipeline cannot extract must publish an empty frame, not
// last frame's. The features run unconditionally every frame, so anything left
// behind gets re-recorded: a stale shadow schedule redraws its tiles, stale
// skinned instances re-dispatch, and the sky paints through a view matrix
// belonging to a camera that may no longer exist.

namespace
{
struct ExtractHarness
{
    EngineConfig Config;
    RuntimeFrameLoop Loop;
    World Entities;
    StoragePartitionSet Partitions;

    RenderExtractContext Context()
    {
        return RenderExtractContext{
            .Config = Config,
            .Runtime = Loop,
            .Presentation = {},
            .Entities = Entities,
            .Partitions = Partitions,
        };
    }
};
} // namespace

TEST(RenderPipelineExtractReset, AbandonedExtractPublishesAnEmptyQueue)
{
    // No asset stores set, so extraction bails at its first check -- the
    // earliest abandon path there is, and the one that used to leave even the
    // draw queue holding the previous frame's items.
    DefaultRenderPipeline pipeline;
    pipeline.GetRenderQueue().AddOpaque(RenderQueueItem{});
    ASSERT_FALSE(pipeline.GetRenderQueue().Opaque().empty());

    ExtractHarness harness;
    RenderExtractContext ctx = harness.Context();
    pipeline.ExtractRender(ctx);

    EXPECT_TRUE(pipeline.GetRenderQueue().Opaque().empty());
}

TEST(RenderPipelineExtractReset, RepeatedAbandonedExtractsStayEmpty)
{
    DefaultRenderPipeline pipeline;
    ExtractHarness harness;

    for (int frame = 0; frame < 3; ++frame)
    {
        pipeline.GetRenderQueue().AddOpaque(RenderQueueItem{});
        RenderExtractContext ctx = harness.Context();
        pipeline.ExtractRender(ctx);
        EXPECT_TRUE(pipeline.GetRenderQueue().Opaque().empty())
            << "frame " << frame << " republished stale draw items";
    }
}
