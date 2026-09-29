// Blendspaces: samples weighted by where facts place the mix, one phase for
// all of them, advanced by the mix's length where it stands.

#include "AnimRigFixture.h"

#include <gtest/gtest.h>

#include <array>
#include <format>
#include <string>

namespace
{
    AnimBoundBlendspace Space(std::uint8_t axes, std::initializer_list<std::array<float, 2>> points)
    {
        AnimBoundBlendspace space;
        space.AxisCount = axes;
        for (const auto& point : points)
        {
            AnimBoundBlendspaceSample sample;
            sample.At[0] = point[0];
            sample.At[1] = point[1];
            space.Samples.push_back(sample);
        }
        return space;
    }

    std::array<float, kAnimBlendspaceMaxSamples> Weights(const AnimBoundBlendspace& space, float x, float y = 0.0f)
    {
        std::array<float, kAnimBlendspaceMaxSamples> weights{};
        AnimBlendspaceWeights(space, { x, y }, weights);
        return weights;
    }
}

TEST(AnimBlendspaceWeights, OneAxisInterpolatesBetweenNeighbours)
{
    const AnimBoundBlendspace space = Space(1, { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 3.0f, 0.0f } });
    auto w = Weights(space, 0.25f);
    EXPECT_NEAR(w[0], 0.75f, 1e-6f);
    EXPECT_NEAR(w[1], 0.25f, 1e-6f);
    EXPECT_NEAR(w[2], 0.0f, 1e-6f);
    w = Weights(space, 2.0f);
    EXPECT_NEAR(w[1], 0.5f, 1e-6f);
    EXPECT_NEAR(w[2], 0.5f, 1e-6f);
    w = Weights(space, 1.0f);
    EXPECT_FLOAT_EQ(w[1], 1.0f) << "on a sample, that sample alone";
    EXPECT_EQ(AnimBlendspaceDominant(space, w), 1u);
}

TEST(AnimBlendspaceWeights, TwoAxesCoverAnyLayoutAndSumToOne)
{
    const AnimBoundBlendspace square =
        Space(2, { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f } });
    auto w = Weights(square, 0.5f, 0.5f);
    for (std::size_t i = 0; i < 4; ++i)
        EXPECT_NEAR(w[i], 0.25f, 1e-6f);
    // Ties go to the lowest index.
    EXPECT_EQ(AnimBlendspaceDominant(square, w), 0u);
    w = Weights(square, 1.0f, 1.0f);
    EXPECT_FLOAT_EQ(w[3], 1.0f);

    // An irregular layout still sums to one everywhere.
    const AnimBoundBlendspace fan =
        Space(2, { { 0.0f, 0.0f }, { 2.0f, 0.5f }, { -1.5f, 1.0f }, { 0.3f, 2.0f }, { 1.0f, -1.0f } });
    for (float x = -1.5f; x <= 2.0f; x += 0.25f)
        for (float y = -1.0f; y <= 2.0f; y += 0.25f)
        {
            w = Weights(fan, x, y);
            float total = 0.0f;
            for (std::size_t i = 0; i < 5; ++i)
            {
                EXPECT_GE(w[i], 0.0f);
                total += w[i];
            }
            EXPECT_NEAR(total, 1.0f, 1e-5f) << x << ", " << y;
        }
}

TEST(AnimBlendspaceData, ABlendspacePlacesDistinctSamplesOnItsAxes)
{
    AnimRigFixture fx;
    const auto error = [&](std::string_view json) { return fx.CompileError(kAnimBlendspaceType, json); };
    constexpr std::string_view kAxis = R"("axes": [ { "fact": "Speed", "min": 0, "max": 4 } ])";
    EXPECT_NE(error(R"({ "axes": [], "samples": [] })").find("$.data.axes"), std::string::npos);
    EXPECT_NE(error(R"({ "axes": [ { "fact": "Speed", "min": 2, "max": 1 } ], "samples": [] })").find("$.data.axes[0]"),
              std::string::npos);
    EXPECT_NE(error(std::string(R"({ )") + std::string(kAxis) + R"(, "samples": [ { "clip": "a", "at": [ 1 ] } ] })")
                  .find("2 to 16 samples"),
              std::string::npos);
    EXPECT_NE(error(std::string(R"({ )") + std::string(kAxis)
                    + R"(, "samples": [ { "clip": "a", "at": [ 1 ] }, { "clip": "b", "at": [ 1, 2 ] } ] })")
                  .find("$.data.samples[1].at"),
              std::string::npos);
    EXPECT_NE(error(std::string(R"({ )") + std::string(kAxis)
                    + R"(, "samples": [ { "clip": "a", "at": [ 1 ] }, { "clip": "b", "at": [ 9 ] } ] })")
                  .find("$.data.samples[1].at[0]"),
              std::string::npos);
    EXPECT_NE(error(std::string(R"({ )") + std::string(kAxis)
                    + R"(, "samples": [ { "clip": "a", "at": [ 1 ] }, { "clip": "b", "at": [ 1 ] } ] })")
                  .find("same point"),
              std::string::npos);
}

namespace
{
    // Locomotion as one blendspace on Speed: walk (1 s) at 1, run (0.5 s) at 3.
    struct BlendspaceFixture : AnimRigFixture
    {
        DataAssetHandle Rig;
        EntityId Entity;

        explicit BlendspaceFixture(std::string_view kind = "cyclic", std::string_view extraBehaviors = {})
        {
            for (const char* tag : { "Anim.Idle", "Anim.Move", "Anim.Sync.Feet" })
                (void)Tags().RegisterTag(tag);
            Clip("asset://anim/idle.sanim", 2.0f);
            Clip("asset://anim/walk.sanim", 1.0f);
            Clip("asset://anim/run.sanim", 0.5f);
            (void)Load("asset://anim/m.facts.sdata", kAnimFactSchemaType, R"({
                "slots": [ { "name": "Speed", "kind": "float" }, { "name": "Crouched", "kind": "bool" } ] })");
            (void)Load("asset://anim/m.space.sdata", kAnimBlendspaceType, R"({
                "axes": [ { "fact": "Speed", "min": 0, "max": 4 } ],
                "samples": [ { "clip": "asset://anim/walk.sanim", "at": [ 1 ] },
                             { "clip": "asset://anim/run.sanim", "at": [ 3 ] } ] })");
            (void)Load("asset://anim/m.behaviors.sdata", kAnimBehaviorSetType,
                       std::format(R"({{ "behaviors": [ {{ "tag": "Anim.Idle", "kind": "cyclic",
                           "sync_group": "Anim.Sync.Feet" }},
                           {{ "tag": "Anim.Move", "kind": "{}", "sync_group": "Anim.Sync.Feet",
                              "blend": {{ "phase": "carry" }} }} {} ] }})",
                                   kind, extraBehaviors));
            (void)Load("asset://anim/m.selector.sdata", kAnimSelectorType, R"({ "rules": [
                { "name": "idle", "priority": 0, "enter": [], "behavior": "Anim.Idle" },
                { "name": "move", "priority": 10, "enter": [ { "fact": "Speed", "compare": "gt", "value": 0.1 } ],
                  "behavior": "Anim.Move" } ] })");
            (void)Load("asset://anim/m.slots.sdata", kAnimSlotMapType, R"({ "rows": [
                { "id": "idle", "behavior": "Anim.Idle", "clip": "asset://anim/idle.sanim" },
                { "id": "move", "behavior": "Anim.Move", "blendspace": "asset://anim/m.space.sdata" } ] })");
            Rig = Load("asset://anim/m.rig.sdata", kAnimRigType, R"({
                "facts": "asset://anim/m.facts.sdata", "behaviors": [ "asset://anim/m.behaviors.sdata" ],
                "slot_maps": [ "asset://anim/m.slots.sdata" ],
                "layers": [ { "name": "anim.layer.base", "selector": "asset://anim/m.selector.sdata",
                              "idle": "Anim.Idle" } ] })");
            Entity = Character(Rig);
        }

        std::string PlayingClip()
        {
            const AnimBoundRig& rig = Bound(Rig);
            const std::uint16_t clip = Playing(Entity).Clip;
            return clip < rig.Contents.size() ? rig.Contents[clip].Path : std::string("(none)");
        }
    };
}

// The phase advances by a tick over the mix's length where the facts put it,
// so the samples stay at one phase as the weights move.
TEST(AnimBlendspace, ThePhaseAdvancesByTheMixsLength)
{
    BlendspaceFixture fx;
    ASSERT_TRUE(fx.Bound(fx.Rig).Valid) << AnimRigFixture::Describe(fx.Bound(fx.Rig));

    fx.Motion(fx.Entity).Speed = 2.0f;
    fx.Tick();
    EXPECT_FLOAT_EQ(fx.Playing(fx.Entity).Phase, 0.0f);
    EXPECT_FLOAT_EQ(fx.Playing(fx.Entity).Coordinates[0], 2.0f);
    // Half walk (1 s) and half run (0.5 s): a 0.75 s mix, 45 ticks a loop.
    fx.Tick(9);
    EXPECT_NEAR(fx.Playing(fx.Entity).Phase, 9.0f / 45.0f, 1e-5f);
    EXPECT_NEAR(fx.Playing(fx.Entity).TimeSeconds, 9.0f / 60.0f, 1e-5f);
    EXPECT_EQ(fx.PlayingClip(), "asset://anim/walk.sanim") << "a tie plays the lower sample's events";

    // Faster: the mix shortens and the phase moves quicker from where it was.
    fx.Motion(fx.Entity).Speed = 3.0f;
    fx.Tick();
    EXPECT_NEAR(fx.Playing(fx.Entity).Phase, 9.0f / 45.0f + 1.0f / 30.0f, 1e-5f);
    EXPECT_EQ(fx.PlayingClip(), "asset://anim/run.sanim");

    // Clamped to the axis: past its end is its end.
    fx.Motion(fx.Entity).Speed = 50.0f;
    fx.Tick();
    EXPECT_FLOAT_EQ(fx.Playing(fx.Entity).Coordinates[0], 4.0f);

    // A full loop wraps.
    fx.Tick(40);
    EXPECT_GE(fx.Playing(fx.Entity).Phase, 0.0f);
    EXPECT_LT(fx.Playing(fx.Entity).Phase, 1.0f);
    EXPECT_FALSE(fx.Playing(fx.Entity).ContentComplete);
}

// A one-shot mix completes when its phase reaches the end.
TEST(AnimBlendspace, AOneShotMixCompletesAtTheEndOfItsPhase)
{
    BlendspaceFixture fx("one_shot");
    fx.Motion(fx.Entity).Speed = 1.0f;
    fx.Tick(60);
    EXPECT_FALSE(fx.Playing(fx.Entity).ContentComplete);
    fx.Tick();
    EXPECT_TRUE(fx.Playing(fx.Entity).ContentComplete);
    EXPECT_FLOAT_EQ(fx.Playing(fx.Entity).Phase, 1.0f);
}

// Carried phase enters a mix at the normalized time the clip had reached.
TEST(AnimBlendspace, PhaseCarriesIntoAMix)
{
    BlendspaceFixture fx;
    fx.Tick(30);
    ASSERT_EQ(fx.BehaviorName(fx.Entity), "Anim.Idle");
    fx.Motion(fx.Entity).Speed = 2.0f;
    fx.Tick();
    // Idle (2 s) was 30 of its 120 ticks in.
    EXPECT_NEAR(fx.Playing(fx.Entity).Phase, 0.25f, 1e-5f);
}

TEST(AnimBlendspaceBinding, AnAxisIsANumericFactAndAMixIsNotAFlow)
{
    {
        AnimRigFixture fx;
        (void)fx.Tags().RegisterTag("Anim.Move");
        fx.Clip("asset://anim/walk.sanim", 1.0f);
        fx.Clip("asset://anim/run.sanim", 0.5f);
        (void)fx.Load("asset://anim/b.facts.sdata", kAnimFactSchemaType,
                      R"({ "slots": [ { "name": "Moving", "kind": "bool" } ] })");
        (void)fx.Load("asset://anim/b.space.sdata", kAnimBlendspaceType, R"({
            "axes": [ { "fact": "Moving", "min": 0, "max": 1 } ],
            "samples": [ { "clip": "asset://anim/walk.sanim", "at": [ 0 ] },
                         { "clip": "asset://anim/run.sanim", "at": [ 1 ] } ] })");
        (void)fx.Load("asset://anim/b.behaviors.sdata", kAnimBehaviorSetType,
                      R"({ "behaviors": [ { "tag": "Anim.Move", "kind": "flow" } ] })");
        (void)fx.Load("asset://anim/b.slots.sdata", kAnimSlotMapType,
                      R"({ "rows": [ { "id": "move", "behavior": "Anim.Move", "blendspace": "asset://anim/b.space.sdata" } ] })");
        const AnimBoundRig& rig = fx.Bound(fx.Load("asset://anim/b.rig.sdata", kAnimRigType, R"({
            "facts": "asset://anim/b.facts.sdata", "behaviors": [ "asset://anim/b.behaviors.sdata" ],
            "slot_maps": [ "asset://anim/b.slots.sdata" ],
            "layers": [ { "name": "anim.layer.base", "idle": "Anim.Move" } ] })"));
        const AnimDiagnostic* axis = AnimRigFixture::FindCode(rig, "anim.blendspace.axis_fact");
        ASSERT_NE(axis, nullptr) << AnimRigFixture::Describe(rig);
        EXPECT_EQ(axis->FieldPath, "$.data.axes[0].fact");
    }
    {
        BlendspaceFixture fx("flow");
        const AnimDiagnostic* behavior = AnimRigFixture::FindCode(fx.Bound(fx.Rig), "anim.blendspace.behavior");
        ASSERT_NE(behavior, nullptr) << AnimRigFixture::Describe(fx.Bound(fx.Rig));
        EXPECT_EQ(behavior->FieldPath, "$.data.rows[1].blendspace");
    }
    EXPECT_NE(AnimRigFixture().CompileError(kAnimSlotMapType, R"({ "rows": [
        { "id": "move", "behavior": "Anim.Move", "clip": "a", "blendspace": "b" } ] })")
                  .find("exactly one of a clip, a flow or a blendspace"),
              std::string::npos);
}

// A mix plays its heaviest sample's marks, so its samples carry one gameplay
// track between them; otherwise the weights would decide what gameplay hears.
TEST(AnimBlendspaceBinding, SamplesShareTheirGameplayEvents)
{
    AnimRigFixture fx({ "Anim.Move" });
    AnimationClipEvent step;
    step.Key = 1;
    step.Time = 0.5f;
    step.Binding = "game.step";
    step.Scope = AnimEventScope::Gameplay;
    fx.Clip("asset://anim/walk.sanim", 1.0f, { step });
    fx.Clip("asset://anim/run.sanim", 0.5f);
    (void)fx.Load("asset://anim/g.facts.sdata", kAnimFactSchemaType,
                  R"({ "slots": [ { "name": "Speed", "kind": "float" } ] })");
    (void)fx.Load("asset://anim/g.space.sdata", kAnimBlendspaceType, R"({
        "axes": [ { "fact": "Speed", "min": 0, "max": 4 } ],
        "samples": [ { "clip": "asset://anim/walk.sanim", "at": [ 1 ] },
                     { "clip": "asset://anim/run.sanim", "at": [ 3 ] } ] })");
    (void)fx.Load("asset://anim/g.behaviors.sdata", kAnimBehaviorSetType,
                  R"({ "behaviors": [ { "tag": "Anim.Move", "kind": "cyclic" } ] })");
    (void)fx.Load("asset://anim/g.slots.sdata", kAnimSlotMapType,
                  R"({ "rows": [ { "id": "move", "behavior": "Anim.Move", "blendspace": "asset://anim/g.space.sdata" } ] })");
    const AnimBoundRig& rig = fx.Bound(fx.Load("asset://anim/g.rig.sdata", kAnimRigType, R"({
        "facts": "asset://anim/g.facts.sdata", "behaviors": [ "asset://anim/g.behaviors.sdata" ],
        "slot_maps": [ "asset://anim/g.slots.sdata" ],
        "layers": [ { "name": "anim.layer.base", "idle": "Anim.Move" } ] })"));
    const AnimDiagnostic* differ = AnimRigFixture::FindCode(rig, "anim.blendspace.gameplay_events");
    ASSERT_NE(differ, nullptr) << AnimRigFixture::Describe(rig);
    EXPECT_EQ(differ->AssetPath, "asset://anim/g.space.sdata");
    EXPECT_FALSE(rig.Valid);
}
