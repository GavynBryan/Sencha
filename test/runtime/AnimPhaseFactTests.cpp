// Content a fact places: where a stride, a gauge or a door's open fraction says,
// rather than where the ticks since it started would put it.

#include "AnimRigFixture.h"

#include <gtest/gtest.h>

#include <anim/AnimPlayback.h>

#include <format>
#include <string>

namespace
{
    constexpr double kTick = AnimRigFixture::kTick;

    // One layer playing "Anim.Play", placed by the Speed fact, over a two-second clip
    // or a mix of two.
    struct PlacedFixture : AnimRigFixture
    {
        DataAssetHandle Rig;
        EntityId Entity;

        explicit PlacedFixture(std::string_view kind = "cyclic", bool mix = false)
            : AnimRigFixture({ "Anim.Play" })
        {
            Clip("asset://anim/two_seconds.sanim", 2.0f);
            Clip("asset://anim/one_second.sanim", 1.0f);
            (void)Load("asset://anim/pf.facts.sdata", kAnimFactSchemaType,
                       R"({ "slots": [ { "name": "Speed", "kind": "float" }, { "name": "Grounded", "kind": "bool" } ] })");
            (void)Load("asset://anim/pf.space.sdata", kAnimBlendspaceType, R"({
                "axes": [ { "fact": "Speed", "min": 0, "max": 1 } ],
                "samples": [ { "clip": "asset://anim/two_seconds.sanim", "at": [ 0 ] },
                             { "clip": "asset://anim/one_second.sanim", "at": [ 1 ] } ] })");
            (void)Load("asset://anim/pf.behaviors.sdata", kAnimBehaviorSetType,
                       std::format(R"({{ "behaviors": [ {{ "tag": "Anim.Play", "kind": "{}", "phase_fact": "Speed" }} ] }})",
                                   kind));
            (void)Load("asset://anim/pf.slots.sdata", kAnimSlotMapType,
                       mix ? R"({ "rows": [ { "id": "play", "behavior": "Anim.Play", "blendspace": "asset://anim/pf.space.sdata" } ] })"
                           : R"({ "rows": [ { "id": "play", "behavior": "Anim.Play", "clip": "asset://anim/two_seconds.sanim" } ] })");
            Rig = Load("asset://anim/pf.rig.sdata", kAnimRigType, R"({
                "facts": "asset://anim/pf.facts.sdata",
                "behaviors": [ "asset://anim/pf.behaviors.sdata" ], "slot_maps": [ "asset://anim/pf.slots.sdata" ],
                "layers": [ { "name": "anim.layer.base", "idle": "Anim.Play" } ] })");
            EXPECT_TRUE(Bound(Rig).Valid) << Describe(Bound(Rig));
            Entity = Character(Rig);
        }

        const AnimLayerContent& At(float fact)
        {
            Motion(Entity).Speed = fact;
            Tick();
            return Playing(Entity);
        }
    };

    AnimPlayback Cycle(float duration)
    {
        return AnimPlayback{ .StartTick = 10, .OffsetSeconds = 0.0f, .Rate = 1.0f, .DurationSeconds = duration,
                             .Cyclic = true };
    }

    // What a pass at `now` crosses, as one stretch of clip seconds.
    std::pair<double, double> Crossed(const AnimPlayback& playback, AnimTick now)
    {
        const AnimCrossing crossing = AnimCrossPlayback(playback, now - 1, AnimPlaybackEntry::Start, now, kTick);
        EXPECT_EQ(crossing.Count, 1u);
        return { crossing.Stretches[0].From, crossing.Stretches[0].To };
    }
}

TEST(AnimPhaseFact, APlaybackFollowsThePhaseForwardAndKeepsItsInstance)
{
    AnimPlayback playback = Cycle(2.0f);
    FollowAnimPlaybackPhase(playback, 0.1f, 10, kTick);
    EXPECT_NEAR(AnimPlaybackSeconds(playback, 10, kTick), 0.2, 1e-6);

    FollowAnimPlaybackPhase(playback, 0.3f, 11, kTick);
    EXPECT_NEAR(AnimPlaybackSeconds(playback, 11, kTick), 0.6, 1e-5);
    const auto [from, to] = Crossed(playback, 11);
    EXPECT_NEAR(from, 0.2, 1e-5);
    EXPECT_NEAR(to, 0.6, 1e-5);
    EXPECT_EQ(playback.StartTick, 10u);
    EXPECT_FLOAT_EQ(playback.OffsetSeconds, 0.2f);

    // Over the wrap it runs on forward, not back across the whole clip.
    FollowAnimPlaybackPhase(playback, 0.9f, 12, kTick);
    FollowAnimPlaybackPhase(playback, 0.05f, 13, kTick);
    EXPECT_NEAR(AnimPlaybackSeconds(playback, 13, kTick), 0.1, 1e-5);
    const auto [wrapFrom, wrapTo] = Crossed(playback, 13);
    EXPECT_NEAR(wrapTo - wrapFrom, 0.3, 1e-5);
}

TEST(AnimPhaseFact, AStepBackwardJumpsAndPlaysNothing)
{
    AnimPlayback playback = Cycle(2.0f);
    FollowAnimPlaybackPhase(playback, 0.5f, 10, kTick);
    FollowAnimPlaybackPhase(playback, 0.6f, 11, kTick);
    FollowAnimPlaybackPhase(playback, 0.4f, 12, kTick);
    EXPECT_NEAR(AnimPlaybackSeconds(playback, 12, kTick), 0.8, 1e-5);
    const auto [from, to] = Crossed(playback, 12);
    EXPECT_DOUBLE_EQ(from, to);

    // Held still, it stays put.
    FollowAnimPlaybackPhase(playback, 0.4f, 13, kTick);
    EXPECT_NEAR(AnimPlaybackSeconds(playback, 13, kTick), 0.8, 1e-5);
}

TEST(AnimPhaseFact, OnlyCyclicOrHeldContentThatMovesNobodyIsPlacedByAFact)
{
    AnimRigFixture fx;
    for (std::string_view refused : {
             R"({ "tag": "Anim.Play", "kind": "one_shot", "phase_fact": "Speed" })",
             R"({ "tag": "Anim.Play", "kind": "flow", "phase_fact": "Speed" })",
             R"({ "tag": "Anim.Play", "kind": "cyclic", "root_motion": true, "phase_fact": "Speed" })",
             R"({ "tag": "Anim.Play", "kind": "cyclic", "rate": 2, "phase_fact": "Speed" })" })
    {
        EXPECT_NE(fx.CompileError(kAnimBehaviorSetType, std::format(R"({{ "behaviors": [ {} ] }})", refused))
                      .find("phase_fact"),
                  std::string::npos)
            << refused;
    }
    EXPECT_TRUE(fx.CompileError(kAnimBehaviorSetType,
                                R"({ "behaviors": [ { "tag": "Anim.Play", "kind": "hold", "phase_fact": "Speed" } ] })")
                    .empty());
}

TEST(AnimPhaseFact, TheFactIsANumberTheRigDeclares)
{
    AnimRigFixture fx({ "Anim.Play" });
    fx.Clip("asset://anim/one_second.sanim", 1.0f);
    (void)fx.Load("asset://anim/pb.facts.sdata", kAnimFactSchemaType,
                  R"({ "slots": [ { "name": "Grounded", "kind": "bool" } ] })");
    (void)fx.Load("asset://anim/pb.behaviors.sdata", kAnimBehaviorSetType,
                  R"({ "behaviors": [ { "tag": "Anim.Play", "kind": "cyclic", "phase_fact": "Grounded" } ] })");
    (void)fx.Load("asset://anim/pb.slots.sdata", kAnimSlotMapType,
                  R"({ "rows": [ { "id": "play", "behavior": "Anim.Play", "clip": "asset://anim/one_second.sanim" } ] })");
    const DataAssetHandle rig = fx.Load("asset://anim/pb.rig.sdata", kAnimRigType, R"({
        "facts": "asset://anim/pb.facts.sdata",
        "behaviors": [ "asset://anim/pb.behaviors.sdata" ], "slot_maps": [ "asset://anim/pb.slots.sdata" ],
        "layers": [ { "name": "anim.layer.base", "idle": "Anim.Play" } ] })");
    const AnimDiagnostic* problem = AnimRigFixture::FindCode(fx.Bound(rig), "anim.behavior.phase_fact");
    ASSERT_NE(problem, nullptr) << AnimRigFixture::Describe(fx.Bound(rig));
    EXPECT_EQ(problem->FieldPath, "$.data.behaviors[0].phase_fact");
    EXPECT_FALSE(fx.Bound(rig).Valid);
}

TEST(AnimPhaseFact, AClipIsWhereTheFactPutsItWhateverTheTicks)
{
    PlacedFixture fx;
    EXPECT_NEAR(fx.At(0.25f).TimeSeconds, 0.5f, 1e-5f);
    // Held: the ticks go by and the clip stays where the fact is.
    for (int tick = 0; tick < 30; ++tick)
        EXPECT_NEAR(fx.At(0.25f).TimeSeconds, 0.5f, 1e-5f);
    EXPECT_NEAR(fx.At(0.75f).TimeSeconds, 1.5f, 1e-5f);
    EXPECT_NEAR(fx.At(1.1f).TimeSeconds, 0.2f, 1e-5f);
    EXPECT_FALSE(fx.Playing(fx.Entity).ContentComplete);
}

TEST(AnimPhaseFact, HeldContentStopsAtEitherEndAndCompletesAtTheLast)
{
    PlacedFixture fx("hold");
    EXPECT_NEAR(fx.At(-0.5f).TimeSeconds, 0.0f, 1e-5f);
    EXPECT_NEAR(fx.At(0.5f).TimeSeconds, 1.0f, 1e-5f);
    EXPECT_FALSE(fx.Playing(fx.Entity).ContentComplete);
    EXPECT_NEAR(fx.At(3.0f).TimeSeconds, 2.0f, 1e-5f);
    EXPECT_TRUE(fx.Playing(fx.Entity).ContentComplete);
}

TEST(AnimPhaseFact, AMixTakesTheFactsPhaseAndHasNoSpeedOfItsOwn)
{
    PlacedFixture fx("cyclic", true);
    const AnimLayerContent& placed = fx.At(0.4f);
    EXPECT_FLOAT_EQ(placed.Phase, 0.4f);
    EXPECT_FLOAT_EQ(placed.Playback.Rate, 0.0f);
    for (int tick = 0; tick < 10; ++tick)
        EXPECT_FLOAT_EQ(fx.At(0.4f).Phase, 0.4f);
    EXPECT_NEAR(fx.At(1.3f).Phase, 0.3f, 1e-5f);
}
