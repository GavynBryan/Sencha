// A clip on its own, as a one-layer rig: speed (backwards, or held), start,
// loop or clamp. Content time is a function of the tick alone.

#include "AnimRigFixture.h"

#include <gtest/gtest.h>

#include <format>
#include <string>

namespace
{
    // One layer, no selector, playing `behavior` -- a JSON behavior object
    // for "Anim.Play" over a one-second clip -- from tick 0.
    struct PlaybackFixture : AnimRigFixture
    {
        EntityId Entity;

        explicit PlaybackFixture(std::string_view behavior)
            : AnimRigFixture({ "Anim.Play" })
        {
            Clip("asset://anim/one_second.sanim", 1.0f);
            (void)Load("asset://anim/p.behaviors.sdata", kAnimBehaviorSetType,
                       std::format(R"({{ "behaviors": [ {} ] }})", behavior));
            (void)Load("asset://anim/p.slots.sdata", kAnimSlotMapType, R"({ "rows": [
                { "id": "play", "behavior": "Anim.Play", "clip": "asset://anim/one_second.sanim" } ] })");
            const DataAssetHandle rig = Load("asset://anim/p.rig.sdata", kAnimRigType, R"({
                "behaviors": [ "asset://anim/p.behaviors.sdata" ], "slot_maps": [ "asset://anim/p.slots.sdata" ],
                "layers": [ { "name": "anim.layer.base", "idle": "Anim.Play" } ] })");
            EXPECT_TRUE(Bound(rig).Valid) << Describe(Bound(rig));
            Entity = Entities.CreateEntity();
            Entities.AddComponent(Entity, AnimRig{ rig });
        }

        // Runs through `seconds` of ticks after tick 0.
        float TimeAfter(double seconds)
        {
            const auto ticks = static_cast<AnimTick>(std::lround(seconds / kTick));
            while (Now <= ticks)
                Tick();
            return Playing(Entity).TimeSeconds;
        }
    };
}

TEST(AnimPlayback, TimeAdvancesByTheTickAndScalesWithSpeed)
{
    PlaybackFixture normal(R"({ "tag": "Anim.Play", "kind": "cyclic" })");
    PlaybackFixture twice(R"({ "tag": "Anim.Play", "kind": "cyclic", "rate": 2.0 })");
    PlaybackFixture held(R"({ "tag": "Anim.Play", "kind": "cyclic", "rate": 0.0, "start_seconds": 0.25 })");
    EXPECT_NEAR(normal.TimeAfter(0.3), 0.3f, 1e-5f);
    EXPECT_NEAR(twice.TimeAfter(0.3), 0.6f, 1e-5f);
    // A held behavior is an authored pose; nothing moves it.
    EXPECT_FLOAT_EQ(held.TimeAfter(0.3), 0.25f);
    EXPECT_FLOAT_EQ(held.TimeAfter(5.0), 0.25f);
    EXPECT_FALSE(held.Playing(held.Entity).ContentComplete);
}

TEST(AnimPlayback, LoopingWrapsAndClampingHolds)
{
    PlaybackFixture looping(R"({ "tag": "Anim.Play", "kind": "cyclic", "start_seconds": 0.9 })");
    PlaybackFixture clamping(R"({ "tag": "Anim.Play", "kind": "one_shot", "start_seconds": 0.9 })");
    PlaybackFixture reversing(R"({ "tag": "Anim.Play", "kind": "cyclic", "rate": -1.0, "start_seconds": 0.1 })");
    EXPECT_NEAR(looping.TimeAfter(0.2), 0.1f, 1e-5f);
    EXPECT_NEAR(clamping.TimeAfter(0.2), 1.0f, 1e-5f);
    EXPECT_TRUE(clamping.Playing(clamping.Entity).ContentComplete);
    // Backwards wraps to the end rather than running negative.
    EXPECT_NEAR(reversing.TimeAfter(0.2), 0.9f, 1e-5f);
    // A long run stays within one loop.
    const float later = looping.TimeAfter(10.0);
    EXPECT_GE(later, 0.0f);
    EXPECT_LT(later, 1.0f);
}

TEST(AnimPlayback, BackwardsOneShotContentEndsAtItsStart)
{
    PlaybackFixture rewinding(R"({ "tag": "Anim.Play", "kind": "one_shot", "rate": -2.0, "start_seconds": 0.5 })");
    EXPECT_NEAR(rewinding.TimeAfter(0.1), 0.3f, 1e-5f);
    EXPECT_FALSE(rewinding.Playing(rewinding.Entity).ContentComplete);
    EXPECT_FLOAT_EQ(rewinding.TimeAfter(0.5), 0.0f);
    EXPECT_TRUE(rewinding.Playing(rewinding.Entity).ContentComplete);
}

TEST(AnimPlayback, AFlowTakesNoSpeed)
{
    AnimRigFixture fx;
    EXPECT_NE(fx.CompileError(kAnimBehaviorSetType, R"({ "behaviors": [
        { "tag": "Anim.Play", "kind": "flow", "rate": 2.0 } ] })")
                  .find("tick clock"),
              std::string::npos);
    EXPECT_NE(fx.CompileError(kAnimBehaviorSetType, R"({ "behaviors": [
        { "tag": "Anim.Play", "kind": "cyclic", "start_seconds": -1.0 } ] })")
                  .find("non-negative"),
              std::string::npos);
}
