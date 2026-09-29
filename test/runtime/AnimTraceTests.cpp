// What the animation console reports: a rig's content risk, and one entity's
// decisions written out as a trace another machine can read.

#include "AnimRigFixture.h"

#include <anim/AnimDecisionLog.h>
#include <anim/AnimRigRisk.h>
#include <anim/AnimTrace.h>
#include <core/console/ConsoleService.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

namespace
{
    struct TraceFixture
    {
        AnimRigFixture Fx;
        DataAssetHandle Rig;

        TraceFixture()
        {
            AnimCharacterRig::RegisterTags(Fx);
            Rig = AnimCharacterRig::Load(Fx);
        }

        static std::string Id(EntityId entity) { return std::format("{}:{}", entity.Index, entity.Generation); }
    };

    bool HasRule(const AnimRigRisk& risk, std::string_view rule)
    {
        return std::ranges::any_of(risk.Findings, [&](const AnimRigRiskFinding& f) { return f.Rule == rule; });
    }

    std::string Joined(const ConsoleResult& result)
    {
        std::string text;
        for (const ConsoleOutputEntry& entry : result.Output)
            text += entry.Text + "\n";
        return text;
    }
}

TEST(AnimRigRisk, TheCharacterRigCountsItsRulesAndRaisesNothing)
{
    TraceFixture fixture;
    const AnimRigRisk risk = MeasureAnimRigRisk(fixture.Fx.Bound(fixture.Rig), &fixture.Fx.Tags());
    EXPECT_EQ(risk.SelectorRules, 6u);
    EXPECT_EQ(risk.DeepestSelector, 6u);
    EXPECT_EQ(risk.BlendOverrides, 0u);
    EXPECT_EQ(risk.LongFlows, 0u);
    EXPECT_TRUE(risk.Findings.empty()) << risk.Findings.front().Message;
}

// An intent the schema declares but no rule reads and no request-keyed layer
// plays: a request for it would be accepted and do nothing.
TEST(AnimRigRisk, AnIntentNothingPlaysIsReported)
{
    TraceFixture fixture;
    fixture.Fx.Reload("asset://anim/character.requests.sdata", kAnimRequestSchemaType, R"({ "intents": [
        { "intent": "anim.intent.reload", "params": [ { "name": "rate", "kind": "float" } ] },
        { "intent": "anim.intent.door_open", "params": [] } ] })");
    const AnimRigRisk risk = MeasureAnimRigRisk(fixture.Fx.Bound(fixture.Rig), &fixture.Fx.Tags());
    ASSERT_TRUE(HasRule(risk, "anim.risk.unclaimed_intent"));
    EXPECT_NE(risk.Findings.front().Message.find("anim.intent.door_open"), std::string::npos);
}

TEST(AnimRigRisk, ABehaviorAFactAndARequestBothSelectIsReported)
{
    TraceFixture fixture;
    fixture.Fx.Reload("asset://anim/character.selector.sdata", kAnimSelectorType, R"({ "rules": [
        { "name": "idle", "priority": 0, "enter": [], "behavior": "Anim.Locomotion.Idle" },
        { "name": "reload", "priority": 50, "enter": [ { "request": "anim.intent.reload" } ],
          "behavior": "Anim.Action.Reload" },
        { "name": "crouch_reload", "priority": 40, "enter": [ { "fact": "Crouched" } ],
          "behavior": "Anim.Action.Reload" } ] })");
    const AnimRigRisk risk = MeasureAnimRigRisk(fixture.Fx.Bound(fixture.Rig), &fixture.Fx.Tags());
    EXPECT_TRUE(HasRule(risk, "anim.risk.fact_and_request"));
}

TEST(AnimTrace, AnEntityWithoutALogHasNoTrace)
{
    TraceFixture fixture;
    const EntityId bare = fixture.Fx.Entities.CreateEntity();
    fixture.Fx.Entities.AddComponent(bare, AnimRig{ fixture.Rig });
    EXPECT_TRUE(WriteAnimTrace(fixture.Fx.Entities, bare, &fixture.Fx.Bound(fixture.Rig)).IsNull());
}

// Names, not ids: the tags and content a trace names mean nothing on another
// machine as numbers.
TEST(AnimTrace, RecordsCarryResolvedNames)
{
    TraceFixture fixture;
    const EntityId walker = fixture.Fx.Character(fixture.Rig, { .Speed = 1.0f });
    fixture.Fx.Tick(3);
    const AnimBoundRig& rig = fixture.Fx.Bound(fixture.Rig);
    const JsonValue trace = WriteAnimTrace(fixture.Fx.Entities, walker, &rig);
    ASSERT_FALSE(trace.IsNull());
    EXPECT_EQ(trace.Find("type")->AsString(), kAnimTraceType);
    EXPECT_EQ(trace.Find("entity")->AsString(), TraceFixture::Id(walker));
    EXPECT_EQ(trace.Find("rig")->AsString(), "asset://anim/character.rig.sdata");
    EXPECT_EQ(trace.Find("captured")->AsString(), "decisions");

    const JsonValue::Array& records = trace.Find("records")->AsArray();
    ASSERT_EQ(records.size(), fixture.Fx.Log(walker).Size());
    const auto entered = std::ranges::find_if(records, [](const JsonValue& record) {
        const JsonValue* behavior = record.Find("behavior");
        return behavior != nullptr && behavior->AsString() == "Anim.Locomotion.Walk";
    });
    ASSERT_NE(entered, records.end());
    EXPECT_EQ(entered->Find("layer_name")->AsString(), "anim.layer.base");
    const auto playing = std::ranges::find_if(records, [](const JsonValue& record) {
        const JsonValue* content = record.Find("content");
        return content != nullptr && content->IsString() && content->AsString() == "asset://anim/walk.sanim";
    });
    EXPECT_NE(playing, records.end());
}

TEST(AnimConsole, TraceStartsRecordingAndExportWritesIt)
{
    TraceFixture fixture;
    ConsoleService console;
    RegisterAnimationConsole(console.Registry(), fixture.Fx.Entities);
    const EntityId walker = fixture.Fx.Entities.CreateEntity();
    fixture.Fx.Entities.AddComponent(walker, AnimRig{ fixture.Rig });
    fixture.Fx.Entities.AddComponent(walker, AnimFacts{});
    fixture.Fx.Entities.AddComponent(walker, AnimTestMotion{ .Speed = 1.0f });
    const std::string id = TraceFixture::Id(walker);
    fixture.Fx.Tick();

    ConsoleResult listed = console.ExecuteLine("anim.trace");
    ASSERT_TRUE(listed.Succeeded()) << Joined(listed);
    EXPECT_NE(Joined(listed).find(id + " asset://anim/character.rig.sdata\n"), std::string::npos) << Joined(listed);

    const std::filesystem::path file = std::filesystem::temp_directory_path() / "sencha_anim_trace.json";
    std::filesystem::remove(file);
    EXPECT_EQ(console.ExecuteLine(std::format("anim.trace.export {} {}", id, file.string())).Status,
              ConsoleStatus::ExecutionFailed)
        << "nothing is recording yet";

    ASSERT_TRUE(console.ExecuteLine("anim.trace " + id).Succeeded());
    EXPECT_NE(Joined(console.ExecuteLine("anim.trace")).find("(recording)"), std::string::npos);
    fixture.Fx.Motion(walker).Speed = 3.0f;
    fixture.Fx.Tick(3);

    ConsoleResult exported = console.ExecuteLine(std::format("anim.trace.export {} {}", id, file.string()));
    ASSERT_TRUE(exported.Succeeded()) << Joined(exported);
    std::ifstream in(file);
    std::stringstream text;
    text << in.rdbuf();
    const std::optional<JsonValue> read = JsonParse(text.str());
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->Find("type")->AsString(), kAnimTraceType);
    EXPECT_FALSE(read->Find("records")->AsArray().empty());
    std::filesystem::remove(file);
}

TEST(AnimConsole, AnEntityThatIsNotAnimatedIsRefused)
{
    TraceFixture fixture;
    ConsoleService console;
    RegisterAnimationConsole(console.Registry(), fixture.Fx.Entities);
    const EntityId plain = fixture.Fx.Entities.CreateEntity();
    EXPECT_EQ(console.ExecuteLine("anim.trace " + TraceFixture::Id(plain)).Status, ConsoleStatus::InvalidArguments);
    EXPECT_EQ(console.ExecuteLine("anim.trace 12").Status, ConsoleStatus::InvalidArguments);
    EXPECT_EQ(console.ExecuteLine("anim.trace 4000:0").Status, ConsoleStatus::InvalidArguments);
}

TEST(AnimConsole, RiskReportsEveryBoundRig)
{
    TraceFixture fixture;
    ConsoleService console;
    RegisterAnimationConsole(console.Registry(), fixture.Fx.Entities);
    (void)fixture.Fx.Bound(fixture.Rig);
    const ConsoleResult report = console.ExecuteLine("anim.risk");
    ASSERT_TRUE(report.Succeeded());
    EXPECT_NE(Joined(report).find("asset://anim/character.rig.sdata: 0 overrides, 6 rules (deepest 6), 0 long flows"),
              std::string::npos)
        << Joined(report);
}
