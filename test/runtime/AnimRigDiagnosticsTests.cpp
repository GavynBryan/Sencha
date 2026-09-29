// Content that cannot do what it says is diagnosed where it binds and reported
// once, whatever tier the entity using it is.

#include "AnimRigFixture.h"

#include <anim/AnimRigCompositionSystem.h>
#include <core/logging/LoggingProvider.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
    struct CapturedLog : ILogSink
    {
        std::vector<std::pair<LogLevel, std::string>>* Lines = nullptr;
        void Write(LogLevel level, std::string_view, std::string_view message) override
        {
            Lines->emplace_back(level, std::string(message));
        }
    };

    std::size_t Mentions(const std::vector<std::pair<LogLevel, std::string>>& lines, std::string_view text)
    {
        std::size_t count = 0;
        for (const auto& [level, line] : lines)
            count += line.find(text) != std::string::npos ? 1u : 0u;
        return count;
    }

    DataAssetHandle LoadRig(AnimRigFixture& fx, std::string_view facts, std::string_view selector,
                            std::string_view slots)
    {
        fx.Clip("asset://anim/idle.sanim", 1.0f);
        (void)fx.Load("asset://anim/d.facts.sdata", kAnimFactSchemaType, facts);
        (void)fx.Load("asset://anim/d.behaviors.sdata", kAnimBehaviorSetType, R"({ "behaviors": [
            { "tag": "Anim.Idle", "kind": "cyclic" },
            { "tag": "Anim.Flinch", "kind": "one_shot", "latch": { "mode": "until_complete" } } ] })");
        (void)fx.Load("asset://anim/d.selector.sdata", kAnimSelectorType, selector);
        (void)fx.Load("asset://anim/d.slots.sdata", kAnimSlotMapType, slots);
        return fx.Load("asset://anim/d.rig.sdata", kAnimRigType, R"({
            "facts": "asset://anim/d.facts.sdata", "behaviors": [ "asset://anim/d.behaviors.sdata" ],
            "slot_maps": [ "asset://anim/d.slots.sdata" ],
            "layers": [ { "name": "anim.layer.base", "selector": "asset://anim/d.selector.sdata",
                          "idle": "Anim.Idle" } ] })");
    }

    constexpr std::string_view kIdleOnly = R"({ "rows": [
        { "id": "idle", "behavior": "Anim.Idle", "clip": "asset://anim/idle.sanim" } ] })";
    constexpr std::string_view kSpeed = R"({ "slots": [ { "name": "Speed", "kind": "float" } ] })";
    constexpr std::string_view kFlinchOnSpeed = R"({ "rules": [
        { "name": "idle", "priority": 0, "enter": [], "behavior": "Anim.Idle" },
        { "name": "flinch", "priority": 10, "enter": [ { "fact": "Speed", "compare": "gt", "value": 1 } ],
          "behavior": "Anim.Flinch" } ] })";
}

// A fact nothing provides keeps its first value; the binding says so until a
// provider is bound, and then stops.
TEST(AnimRigDiagnostics, AFactNothingProvidesIsDiagnosedUntilOneIs)
{
    AnimRigFixture fx({ "Anim.Idle", "Anim.Flinch" });
    const DataAssetHandle rig = LoadRig(fx, R"({ "slots": [ { "name": "Mystery", "kind": "float" } ] })",
                                        R"({ "rules": [ { "name": "idle", "enter": [], "behavior": "Anim.Idle" } ] })",
                                        kIdleOnly);
    const AnimDiagnostic* unprovided = AnimRigFixture::FindCode(fx.Bound(rig), "anim.fact.unprovided");
    ASSERT_NE(unprovided, nullptr) << AnimRigFixture::Describe(fx.Bound(rig));
    EXPECT_EQ(unprovided->AssetPath, "asset://anim/d.facts.sdata");
    EXPECT_TRUE(fx.Bound(rig).Valid) << "a warning, not an error";

    (void)fx.Entities.GetResource<AnimFactProviders>().BindField<&AnimTestMotion::Speed>("Mystery");
    EXPECT_EQ(AnimRigFixture::FindCode(fx.Bound(rig), "anim.fact.unprovided"), nullptr);
}

// A rule selecting a behavior no row plays is diagnosed, and its one-shot ends
// at once rather than holding the layer under its latch.
TEST(AnimRigDiagnostics, ABehaviorNoRowPlaysIsDiagnosedAndCannotHoldALatch)
{
    AnimRigFixture fx({ "Anim.Idle", "Anim.Flinch" });
    const DataAssetHandle rig = LoadRig(fx, kSpeed, kFlinchOnSpeed, kIdleOnly);
    const AnimDiagnostic* noRow = AnimRigFixture::FindCode(fx.Bound(rig), "anim.slot.no_row");
    ASSERT_NE(noRow, nullptr) << AnimRigFixture::Describe(fx.Bound(rig));
    EXPECT_EQ(noRow->AssetPath, "asset://anim/d.selector.sdata");
    EXPECT_EQ(noRow->FieldPath, "$.data.rules[1].behavior");

    const EntityId walker = fx.Character(rig, { .Speed = 2.0f });
    fx.Tick(3);
    ASSERT_EQ(fx.Selection(walker).Behavior, fx.Tag("Anim.Flinch"));
    fx.Motion(walker).Speed = 0.0f;
    fx.Tick(3);
    EXPECT_EQ(fx.Selection(walker).Behavior, fx.Tag("Anim.Idle"));
}

// Every rig reports its diagnostics once per binding, whatever its entities
// carry: a prop's failed rig is heard as surely as a character's.
TEST(AnimRigDiagnostics, APropRigsFailureIsLoggedOnce)
{
    AnimRigFixture fx({ "Anim.Door.Open" });
    fx.Clip("asset://anim/door.sanim", 1.0f);
    (void)fx.Load("asset://anim/p.behaviors.sdata", kAnimBehaviorSetType,
                  R"({ "behaviors": [ { "tag": "Anim.Door.Open", "kind": "one_shot" } ] })");
    (void)fx.Load("asset://anim/p.slots.sdata", kAnimSlotMapType,
                  R"({ "rows": [ { "id": "open", "behavior": "Anim.Door.Open", "clip": "asset://anim/door.sanim" } ] })");
    const DataAssetHandle rig = fx.Load("asset://anim/p.rig.sdata", kAnimRigType, R"({
        "behaviors": [ "asset://anim/p.behaviors.sdata" ], "slot_maps": [ "asset://anim/p.slots.sdata" ],
        "layers": [ { "name": "anim.layer.nowhere" } ] })");
    ASSERT_FALSE(fx.Bound(rig).Valid);

    std::vector<std::pair<LogLevel, std::string>> lines;
    LoggingProvider logging;
    logging.AddSink<CapturedLog>().Lines = &lines;
    AnimRigCompositionSystem composition(true, &logging);
    for (int i = 0; i < 2; ++i)
        (void)fx.Character(rig);
    for (int tick = 0; tick < 5; ++tick)
        composition.Compose(fx.Entities);

    EXPECT_EQ(Mentions(lines, "anim.layer.nowhere"), 1u);
}

// A rig handle that names another kind of asset binds as an invalid rig that
// says what it is, rather than as nothing.
TEST(AnimRigDiagnostics, AHandleToAnotherKindOfAssetSaysSo)
{
    AnimRigFixture fx;
    const DataAssetHandle notARig = fx.Load("asset://anim/x.behaviors.sdata", kAnimBehaviorSetType,
                                            R"({ "behaviors": [] })");
    const AnimBoundRig* bound = fx.Bindings().Resolve(notARig, fx.Entities);
    ASSERT_NE(bound, nullptr);
    EXPECT_FALSE(bound->Valid);
    EXPECT_NE(AnimRigFixture::FindCode(*bound, "anim.asset.wrong_subtype"), nullptr);
}
