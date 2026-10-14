// The Workbench's preview: the normative charge run on a fixture character with
// the production schedule, driven by a timed script; rewinding replays the script
// to the same ticks, acting at a past tick cuts what came after it, and the stage
// draws where the pawn went.

#include "authoring/AbilityPreviewControls.h"
#include "authoring/AbilityPreviewInspector.h"
#include "authoring/AbilityPreviewLanes.h"
#include "authoring/AbilityPreviewSession.h"
#include "authoring/AbilityRecipes.h"
#include "authoring/AbilityStageStrokes.h"

#include <abilities/AbilityDefinition.h>
#include <abilities/AbilityRegistry.h>
#include <assets/runtime/RuntimeAssets.h>
#include <attributes/AttributeRegistry.h>
#include <core/assets/AssetRegistry.h>
#include <core/json/JsonParser.h>
#include <ecs/World.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <random>

namespace
{
constexpr std::string_view kCharge = "asset://abilities/charge_release.sdata";

struct AbilityPreviewTest : testing::Test
{
    std::filesystem::path Root = std::filesystem::temp_directory_path()
        / ("sencha_ability_preview_" + std::to_string(std::random_device{}()));
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets{ Logging, Serializers };
    std::unique_ptr<AbilityPreviewSession> Preview;
    JsonValue Data;

    void SetUp() override
    {
        std::filesystem::copy(std::filesystem::path(SENCHA_REPO_ROOT) / "test/fixtures/abilities/assets", Root,
                              std::filesystem::copy_options::recursive);
        (void)ScanAssetsDirectory(Root.generic_string(), Assets.Registry, Assets.Assets.Kinds());
        Data = *JsonParseFile(Root / "abilities" / "charge_release.sdata")->Find("data");
        Preview = std::make_unique<AbilityPreviewSession>(Assets, std::function<void(World&)>{});
        Preview->Show(std::string(kCharge), Data);
    }

    void TearDown() override
    {
        Preview.reset();
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }

    void Control(AbilityPreviewControl control, std::string name = "Input.PrimaryFire", double value = 0.0)
    {
        Preview->Control({ .Control = control, .Name = std::move(name), .Value = value });
        Preview->Step();
    }

    // Charge mode on, pressed at tick 2, released at tick 13: a release after
    // eleven ticks of charging, past the six the fire transition asks for.
    void ChargeAndRelease()
    {
        Control(AbilityPreviewControl::GrantTag, "Weapon.CTX.Mode.Charge");
        Control(AbilityPreviewControl::Press);
        Preview->RunTo(12);
        Control(AbilityPreviewControl::Release);
        Preview->RunTo(20);
    }

    [[nodiscard]] const AbilityPreviewTickRecord& At(std::uint64_t tick) const
    {
        return *std::ranges::find(Preview->History(), tick, &AbilityPreviewTickRecord::Tick);
    }

    [[nodiscard]] bool Traced(std::uint64_t tick, AbilityTraceEvent event) const
    {
        return std::ranges::any_of(At(tick).Trace, [&](const AbilityTraceRecord& record) { return record.Event == event; });
    }

    [[nodiscard]] float Ammo(std::uint64_t tick) const
    {
        const AttributeId ammo =
            Preview->SimulationWorld()->GetResource<AttributeRegistry>().FindAttribute("Ammo");
        return At(tick).Attributes[ammo.Value - 1];
    }
};
}

TEST_F(AbilityPreviewTest, APressAndAReleaseFireTheChargeOnTheFixture)
{
    ASSERT_TRUE(Preview->Problems().empty()) << Preview->Problems().front();
    ASSERT_EQ(Preview->InputActions(), std::vector<std::string>{ "Input.PrimaryFire" });
    ChargeAndRelease();
    EXPECT_TRUE(Traced(2, AbilityTraceEvent::Started));
    EXPECT_TRUE(Traced(13, AbilityTraceEvent::Transition)) << "the release takes it to Fire";
    EXPECT_LT(Ammo(14), Ammo(12)) << "and Fire's commit paid its ammo";
}

TEST_F(AbilityPreviewTest, RewindingReplaysTheScriptToTheSameTicks)
{
    ChargeAndRelease();
    const AbilityPreviewTickRecord before = At(15);
    Preview->RunTo(5);
    EXPECT_EQ(Preview->Tick(), 5u);
    Preview->RunTo(20);
    const AbilityPreviewTickRecord& after = At(15);
    ASSERT_EQ(after.Trace.size(), before.Trace.size());
    EXPECT_EQ(after.Attributes, before.Attributes);
    ASSERT_EQ(after.Records.size(), before.Records.size());
    for (std::size_t i = 0; i < after.Records.size(); ++i)
    {
        EXPECT_EQ(after.Records[i].Phase, before.Records[i].Phase);
        EXPECT_EQ(after.Records[i].State, before.Records[i].State);
    }
}

TEST_F(AbilityPreviewTest, ActingAtAPastTickCutsWhatCameAfter)
{
    ChargeAndRelease();
    Preview->RunTo(8);
    Control(AbilityPreviewControl::Interrupt);
    ASSERT_EQ(Preview->Script().size(), 3u);
    EXPECT_EQ(Preview->Script().back().Control, AbilityPreviewControl::Interrupt);
    EXPECT_EQ(Preview->Script().back().Tick, 9u) << "the release at 13 is gone; the interrupt is what happened next";
    Preview->RunTo(20);
    EXPECT_FALSE(Traced(13, AbilityTraceEvent::Transition));
}

TEST_F(AbilityPreviewTest, AWorkingEditReplaysToWhereThePreviewWas)
{
    ChargeAndRelease();
    JsonValue edited = Data;
    *edited.Find("phases")->AsArray()[2].Find("then")->AsArray()[0].Find("ticks") = JsonValue(40);
    Preview->Show(std::string(kCharge), edited);
    EXPECT_EQ(Preview->Tick(), 20u);
    ASSERT_NE(Preview->Definition(), nullptr);
    EXPECT_EQ(Preview->Script().size(), 3u) << "the script survives an edit";
}

TEST_F(AbilityPreviewTest, DataThatDoesNotCompileSaysWhyAndRunsNothing)
{
    JsonValue broken = Data;
    *broken.Find("phases")->AsArray()[0].Find("then")->AsArray()[0].Find("to") = JsonValue("p_nowhere");
    Preview->Show(std::string(kCharge), broken);
    EXPECT_FALSE(Preview->Problems().empty());
    Preview->RunTo(10);
    EXPECT_TRUE(Preview->History().empty());
}

TEST_F(AbilityPreviewTest, TheInspectorNamesWhatTheTickDidFromTheWorkingData)
{
    ChargeAndRelease();
    Preview->RunTo(13);
    AbilityPreviewInspector inspector(*Preview, [this] { return &Data; });
    const std::vector<PropertyNode> nodes = inspector.Nodes();
    const auto event = std::ranges::find_if(nodes, [](const PropertyNode& node) { return node.Descriptor.Label == "Transition"; });
    ASSERT_NE(event, nodes.end());
    EXPECT_EQ(event->Value, "\xE2\x86\x92 Fire");
    const auto ran = std::ranges::find_if(nodes, [](const PropertyNode& node) {
        return node.Descriptor.Label == "Ran" && node.Value.starts_with("physics.trace");
    });
    EXPECT_NE(ran, nodes.end()) << "Fire's trace ran on the same tick";
}

TEST_F(AbilityPreviewTest, TheControlsScriptWhatThePersonDoes)
{
    AbilityPreviewControls controls(*Preview);
    ASSERT_EQ(controls.Act("input.Input.PrimaryFire", "tap3", {}), PropertyEditResult::Applied);
    ASSERT_EQ(controls.CommitEdit("attribute.Ammo", "2"), PropertyEditResult::Applied);
    ASSERT_EQ(controls.Act("attribute.Ammo", "plot", {}), PropertyEditResult::Applied);
    ASSERT_EQ(Preview->Script().size(), 2u);
    EXPECT_EQ(Preview->Script().front().Control, AbilityPreviewControl::Tap);
    EXPECT_EQ(Preview->Script().front().Count, 3u);
    EXPECT_EQ(Preview->Script().front().Tick, Preview->Tick() + 1) << "on the next tick";
    EXPECT_EQ(controls.Plotted(), "Ammo");
}

TEST_F(AbilityPreviewTest, TheDeckShowsEachPhaseStayAndTheInputItWasGiven)
{
    ChargeAndRelease();
    const WorkspaceTimelineFacet facet = AbilityPreviewTimeline(*Preview, &Data, "Ammo");
    ASSERT_GE(facet.Lanes.size(), 2u);
    const std::vector<TimelineSpan>& spans = facet.Lanes.front().Spans;
    const auto charge = std::ranges::find(spans, std::string("Charge"), &TimelineSpan::Label);
    ASSERT_NE(charge, spans.end());
    EXPECT_DOUBLE_EQ(charge->Start, 2.0 / AbilityPreviewSession::kTickRate);
    EXPECT_DOUBLE_EQ(charge->End, 13.0 / AbilityPreviewSession::kTickRate);
    EXPECT_NE(std::ranges::find(facet.Marks, std::string("press Input.PrimaryFire"), &TimelineMark::Label),
              facet.Marks.end());
    EXPECT_EQ(facet.Lanes[1].Label, "Ammo");
    EXPECT_FALSE(facet.Lanes[1].Samples.empty());
}

TEST_F(AbilityPreviewTest, ATimedMovementCarriesThePawnAlongItsAimAndTheStageFollows)
{
    Preview->Show("asset://abilities/dash.sdata", FindAbilityRecipe("Timed Movement")->Make());
    ASSERT_TRUE(Preview->Problems().empty()) << Preview->Problems().front();
    Preview->RunTo(5);
    Control(AbilityPreviewControl::Press);
    Preview->RunTo(30);
    const Vec3d before = At(5).Pose.Position;
    const Vec3d after = At(30).Pose.Position;
    const Vec3d moved = after - before;
    EXPECT_GT(moved.Dot(At(5).AimDirection), 2.0f) << "twelve ticks at twelve metres a second, along the aim";
    EXPECT_NEAR(after.Y, before.Y, 0.05f) << "and on the floor, as the character's own mover keeps it";

    const AbilityStage stage = AbilityStageOf(*Preview, Preview->Tick());
    EXPECT_FALSE(stage.Strokes.empty());
    EXPECT_GE(stage.Bounds.HalfExtent().Magnitude() * 2.0f, moved.Magnitude()) << "the stage frames the whole trail";
    const AbilityStage earlier = AbilityStageOf(*Preview, 5);
    EXPECT_LT(earlier.Bounds.HalfExtent().Magnitude(), stage.Bounds.HalfExtent().Magnitude())
        << "a past tick shows the path only so far";
}

TEST_F(AbilityPreviewTest, TheStageShowsThePlacedTargetAndWhatTheChargeStruck)
{
    Control(AbilityPreviewControl::PlaceTarget, {}, 6.0);
    ChargeAndRelease();
    ASSERT_EQ(At(1).Placed.size(), 1u);
    Preview->RunTo(13);
    ASSERT_FALSE(At(13).Hits.empty()) << "Fire's trace struck the target";
    const AbilityStage stage = AbilityStageOf(*Preview, 13);
    const Vec3d hit = At(13).Hits.front().Point;
    EXPECT_TRUE(stage.Bounds.Contains(hit));
    EXPECT_TRUE(stage.Bounds.Contains(At(1).Placed.front()));
}
