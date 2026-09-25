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
    struct TracedHero
    {
        AnimRigFixture Fx;
        DataAssetHandle Rig;

        TracedHero()
        {
            AnimHero::RegisterTags(Fx);
            Rig = AnimHero::Load(Fx);
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

TEST(AnimRigRisk, TheHeroCountsItsRulesAndRaisesNothing)
{
    TracedHero hero;
    const AnimRigRisk risk = MeasureAnimRigRisk(hero.Fx.Bound(hero.Rig), &hero.Fx.Tags());
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
    TracedHero hero;
    hero.Fx.Reload("asset://anim/hero.requests.sdata", kAnimRequestSchemaType, R"({ "intents": [
        { "intent": "anim.intent.reload", "params": [ { "name": "rate", "kind": "float" } ] },
        { "intent": "anim.intent.door_open", "params": [] } ] })");
    const AnimRigRisk risk = MeasureAnimRigRisk(hero.Fx.Bound(hero.Rig), &hero.Fx.Tags());
    ASSERT_TRUE(HasRule(risk, "anim.risk.unclaimed_intent"));
    EXPECT_NE(risk.Findings.front().Message.find("anim.intent.door_open"), std::string::npos);
}

TEST(AnimRigRisk, ABehaviorAFactAndARequestBothSelectIsReported)
{
    TracedHero hero;
    hero.Fx.Reload("asset://anim/hero.selector.sdata", kAnimSelectorType, R"({ "rules": [
        { "name": "idle", "priority": 0, "enter": [], "behavior": "Anim.Locomotion.Idle" },
        { "name": "reload", "priority": 50, "enter": [ { "request": "anim.intent.reload" } ],
          "behavior": "Anim.Action.Reload" },
        { "name": "crouch_reload", "priority": 40, "enter": [ { "fact": "Crouched" } ],
          "behavior": "Anim.Action.Reload" } ] })");
    const AnimRigRisk risk = MeasureAnimRigRisk(hero.Fx.Bound(hero.Rig), &hero.Fx.Tags());
    EXPECT_TRUE(HasRule(risk, "anim.risk.fact_and_request"));
}

TEST(AnimTrace, AnEntityWithoutALogHasNoTrace)
{
    TracedHero hero;
    const EntityId bare = hero.Fx.Entities.CreateEntity();
    hero.Fx.Entities.AddComponent(bare, AnimRig{ hero.Rig });
    EXPECT_TRUE(WriteAnimTrace(hero.Fx.Entities, bare, &hero.Fx.Bound(hero.Rig)).IsNull());
}

// Names, not ids: the tags and content a trace names mean nothing on another
// machine as numbers.
TEST(AnimTrace, RecordsCarryResolvedNames)
{
    TracedHero hero;
    const EntityId walker = hero.Fx.Character(hero.Rig, { .Speed = 1.0f });
    hero.Fx.Tick(3);
    const AnimBoundRig& rig = hero.Fx.Bound(hero.Rig);
    const JsonValue trace = WriteAnimTrace(hero.Fx.Entities, walker, &rig);
    ASSERT_FALSE(trace.IsNull());
    EXPECT_EQ(trace.Find("type")->AsString(), kAnimTraceType);
    EXPECT_EQ(trace.Find("entity")->AsString(), TracedHero::Id(walker));
    EXPECT_EQ(trace.Find("rig")->AsString(), "asset://anim/hero.rig.sdata");
    EXPECT_EQ(trace.Find("captured")->AsString(), "decisions");

    const JsonValue::Array& records = trace.Find("records")->AsArray();
    ASSERT_EQ(records.size(), hero.Fx.Log(walker).Size());
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
    TracedHero hero;
    ConsoleService console;
    RegisterAnimationConsole(console.Registry(), hero.Fx.Entities);
    const EntityId walker = hero.Fx.Entities.CreateEntity();
    hero.Fx.Entities.AddComponent(walker, AnimRig{ hero.Rig });
    hero.Fx.Entities.AddComponent(walker, AnimFacts{});
    hero.Fx.Entities.AddComponent(walker, AnimTestMotion{ .Speed = 1.0f });
    const std::string id = TracedHero::Id(walker);
    hero.Fx.Tick();

    ConsoleResult listed = console.ExecuteLine("anim.trace");
    ASSERT_TRUE(listed.Succeeded()) << Joined(listed);
    EXPECT_NE(Joined(listed).find(id + " asset://anim/hero.rig.sdata\n"), std::string::npos) << Joined(listed);

    const std::filesystem::path file = std::filesystem::temp_directory_path() / "sencha_anim_trace.json";
    std::filesystem::remove(file);
    EXPECT_EQ(console.ExecuteLine(std::format("anim.trace.export {} {}", id, file.string())).Status,
              ConsoleStatus::ExecutionFailed)
        << "nothing is recording yet";

    ASSERT_TRUE(console.ExecuteLine("anim.trace " + id).Succeeded());
    EXPECT_NE(Joined(console.ExecuteLine("anim.trace")).find("(recording)"), std::string::npos);
    hero.Fx.Motion(walker).Speed = 3.0f;
    hero.Fx.Tick(3);

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
    TracedHero hero;
    ConsoleService console;
    RegisterAnimationConsole(console.Registry(), hero.Fx.Entities);
    const EntityId plain = hero.Fx.Entities.CreateEntity();
    EXPECT_EQ(console.ExecuteLine("anim.trace " + TracedHero::Id(plain)).Status, ConsoleStatus::InvalidArguments);
    EXPECT_EQ(console.ExecuteLine("anim.trace 12").Status, ConsoleStatus::InvalidArguments);
    EXPECT_EQ(console.ExecuteLine("anim.trace 4000:0").Status, ConsoleStatus::InvalidArguments);
}

TEST(AnimConsole, RiskReportsEveryBoundRig)
{
    TracedHero hero;
    ConsoleService console;
    RegisterAnimationConsole(console.Registry(), hero.Fx.Entities);
    (void)hero.Fx.Bound(hero.Rig);
    const ConsoleResult report = console.ExecuteLine("anim.risk");
    ASSERT_TRUE(report.Succeeded());
    EXPECT_NE(Joined(report).find("asset://anim/hero.rig.sdata: 0 overrides, 6 rules (deepest 6), 0 long flows"),
              std::string::npos)
        << Joined(report);
}
