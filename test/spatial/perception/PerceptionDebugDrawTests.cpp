// perception.debug.draw fills the perception channel each tick and empties it
// once when turned off.

#include "PerceptionHarness.h"

#include <debug/DebugLines.h>

namespace
{
    const DebugLineList& PerceptionLines(PerceptionHarness& harness)
    {
        DebugLineChannels& channels = harness.Entities.GetResource<DebugLineChannels>();
        return channels.Lines(channels.Channel("perception"));
    }
}

TEST(PerceptionDebugDraw, NothingIsDrawnUntilAsked)
{
    PerceptionHarness harness;
    (void)harness.Observer(Vec3d::Zero());
    (void)harness.Target(Vec3d(0, 0, -5));
    harness.Step();
    EXPECT_FALSE(harness.Entities.HasResource<DebugLineChannels>());
    EXPECT_TRUE(harness.System.LastTrace().empty());
}

TEST(PerceptionDebugDraw, DrawingShowsConesSightLinesAndClearsWhenTurnedOff)
{
    PerceptionHarness harness;
    (void)harness.Observer(Vec3d::Zero());
    (void)harness.Target(Vec3d(0, 0, -5));
    (void)harness.Target(Vec3d(0, 0, -10));
    (void)harness.Box(Vec3d(0, 0, -7), Vec3d(2, 2, 0.5f));

    harness.SetCVar("perception.debug.draw", CVarValue{ true });
    harness.Step();
    const DebugLineList& lines = PerceptionLines(harness);
    // The cone, a seen line and a blocked line among the depth-tested ones.
    EXPECT_GE(lines.Tested().size(), 28u + 2u);
    // The facing tick, the occluded remainder, and a marker on what was seen.
    EXPECT_GE(lines.OnTop().size(), 1u + 1u + 3u);

    harness.SetCVar("perception.debug.draw", CVarValue{ false });
    harness.Step();
    EXPECT_TRUE(PerceptionLines(harness).Empty());
}
