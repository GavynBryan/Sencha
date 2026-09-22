// The animation preview runs a rig under a scenario with the production
// binding, gather, derivation and request code. What the author does live and
// what a replay of the saved scenario does must be the same run, and nothing a
// scenario names is ever registered or written back into content.

#include "authoring/AnimationPreviewSession.h"
#include "authoring/AnimationRigOutline.h"
#include "authoring/AnimationScenario.h"

#include <anim/AnimFactSchema.h>
#include <anim/AnimRequestSchema.h>
#include <anim/AnimRigData.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <core/json/JsonParser.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace
{
    constexpr std::string_view kRig = "asset://animation/hero.rig.sdata";

    struct PreviewFixture
    {
        DataAssetTypeRegistry Types;
        DataSchemaRegistry Schemas;
        DataAssetCache Data;

        PreviewFixture()
        {
            RegisterAnimFactSchema(Types, Schemas);
            RegisterAnimRequestSchema(Types, Schemas);
            RegisterAnimRigData(Types, Schemas);
            Load("asset://animation/engine.facts.sdata", kAnimFactSchemaType, R"({
                "slots": [ { "name": "Grounded", "kind": "bool" },
                           { "name": "Speed", "kind": "float" },
                           { "name": "VerticalSpeed", "kind": "float" },
                           { "name": "Dead", "kind": "bool" } ] })");
            Load("asset://animation/hero.facts.sdata", kAnimFactSchemaType, R"({
                "extends": "asset://animation/engine.facts.sdata",
                "slots": [ { "name": "Stance", "kind": "tag" } ],
                "derived": [
                    { "name": "JustLanded", "op": "edge", "source": { "fact": "Grounded" },
                      "direction": "rising", "window_ms": 120 },
                    { "name": "Airborne", "op": "min_duration",
                      "source": { "fact": "Grounded", "not": true }, "window_ms": 80 },
                    { "name": "Moving", "op": "hysteresis", "source": { "fact": "Speed" },
                      "enter": 1.0, "exit": 0.5 } ] })");
            Load("asset://animation/hero.requests.sdata", kAnimRequestSchemaType, R"({
                "intents": [
                    { "intent": "anim.intent.reload", "params": [ { "name": "rate", "kind": "float" } ] },
                    { "intent": "anim.intent.flinch", "params": [] } ] })");
            Load(kRig, kAnimRigType, R"({
                "facts": "asset://animation/hero.facts.sdata",
                "requests": "asset://animation/hero.requests.sdata",
                "layers": [ { "name": "anim.layer.base" } ] })");
        }

        void Load(std::string_view path, std::string_view type, std::string_view json)
        {
            const DataAssetCompileResult compiled = Types.Find(type)->Compile(*JsonParse(json));
            ASSERT_TRUE(compiled.IsValid()) << path << ": " << compiled.Error;
            (void)Data.Register(path, std::string(type), compiled.Value);
        }

        static void Vocabulary(World& world)
        {
            GameplayTagRegistry& tags = world.GetResource<GameplayTagRegistry>();
            for (const char* name : { "anim.intent.reload", "anim.intent.flinch", "stance.crouch" })
                (void)tags.RegisterTag(name);
        }

        static AnimationScenario Scenario()
        {
            AnimationScenario scenario;
            scenario.Name = "landing";
            scenario.RigPath = std::string(kRig);
            scenario.Participants = { "player", "ai" };
            scenario.Inputs = { { "Grounded", AnimationScenarioValue::FromBool(true) } };
            return scenario;
        }
    };

    float FactFloat(const AnimationPreviewSession& session, std::string_view fact)
    {
        return AnimFactToFloat(session.Facts()[static_cast<std::size_t>(session.Rig()->FindSlot(fact))]);
    }

    bool FactBool(const AnimationPreviewSession& session, std::string_view fact)
    {
        return AnimFactToBool(session.Facts()[static_cast<std::size_t>(session.Rig()->FindSlot(fact))]);
    }

    bool HasProblem(const AnimationPreviewSession& session, std::string_view code, std::string_view field)
    {
        for (const AnimDiagnostic& problem : session.ScenarioProblems())
        {
            if (problem.Code == code && problem.FieldPath == field)
                return true;
        }
        return false;
    }

    AnimationScenarioAction Issue(std::string participant, std::string intent,
                                  AnimRequestLifetime lifetime = AnimRequestLifetime::Held)
    {
        AnimationScenarioAction action;
        action.Participant = std::move(participant);
        action.Intent = std::move(intent);
        action.Lifetime = lifetime;
        return action;
    }
}

// ─── Scenario format ────────────────────────────────────────────────────────

TEST(AnimationScenario, RoundTripsAndKeepsWhatItDoesNotUnderstand)
{
    const auto document = JsonParse(R"({
        "type": "animation.preview_scenario", "version": 1, "name": "reload",
        "rig": "asset://animation/hero.rig.sdata", "tick_rate": 30, "seed": 7,
        "participants": [ "player" ],
        "inputs": { "Speed": 2.5, "Stance": "stance.crouch" },
        "actions": [
            { "tick": 3, "set": "Grounded", "value": false, "note": "jump" },
            { "tick": 5, "issue": "anim.intent.reload", "source": "player", "lifetime": "fixed",
              "ticks": 12, "layers": 2, "params": { "rate": 1.5 } },
            { "tick": 9, "cancel": "anim.intent.reload", "source": "player", "reason": "interrupted" } ],
        "camera": { "yaw": 30 } })");
    ASSERT_TRUE(document.has_value());

    std::vector<AnimDiagnostic> diagnostics;
    const std::optional<AnimationScenario> scenario =
        ReadAnimationScenario(*document, "reload.sanimscenario", diagnostics);
    ASSERT_TRUE(scenario.has_value());
    EXPECT_TRUE(diagnostics.empty());
    EXPECT_EQ(scenario->TickRate, 30u);
    ASSERT_EQ(scenario->Actions.size(), 3u);
    EXPECT_EQ(scenario->Actions[1].Lifetime, AnimRequestLifetime::Fixed);
    EXPECT_EQ(scenario->Actions[1].FixedTicks, 12u);
    EXPECT_EQ(scenario->Actions[1].Layers, 2u);
    EXPECT_EQ(scenario->Actions[2].Reason, AnimCancelReason::Interrupted);
    ASSERT_EQ(scenario->Actions[0].Unknown.size(), 1u);
    ASSERT_EQ(scenario->Unknown.size(), 1u);

    std::vector<AnimDiagnostic> again;
    const std::optional<AnimationScenario> reread =
        ReadAnimationScenario(WriteAnimationScenario(*scenario), "reload.sanimscenario", again);
    ASSERT_TRUE(reread.has_value());
    EXPECT_TRUE(again.empty());
    EXPECT_EQ(WriteAnimationScenario(*reread).AsObject().size(),
              WriteAnimationScenario(*scenario).AsObject().size());
    EXPECT_EQ(reread->Actions[0].Unknown[0].first, "note");
    EXPECT_EQ(reread->Unknown[0].first, "camera");
}

TEST(AnimationScenario, ProblemsAreLocated)
{
    const auto document = JsonParse(R"({
        "type": "animation.preview_scenario", "version": 1,
        "participants": [ "player" ],
        "actions": [
            { "tick": 1, "issue": "anim.intent.reload", "source": "stranger" },
            { "tick": 2, "issue": "anim.intent.reload", "source": "player", "lifetime": "forever" } ] })");
    std::vector<AnimDiagnostic> diagnostics;
    (void)ReadAnimationScenario(*document, "bad.sanimscenario", diagnostics);
    ASSERT_EQ(diagnostics.size(), 2u);
    EXPECT_EQ(diagnostics[0].FieldPath, "$.actions[0].source");
    EXPECT_EQ(diagnostics[1].FieldPath, "$.actions[1].lifetime");
    EXPECT_EQ(diagnostics[1].AssetPath, "bad.sanimscenario");
}

// ─── Session ────────────────────────────────────────────────────────────────

TEST(AnimationPreviewSession, LiveEditsApplyOnTheNextTick)
{
    PreviewFixture fx;
    AnimationPreviewSession session(fx.Data, &PreviewFixture::Vocabulary);
    ASSERT_TRUE(session.Open(PreviewFixture::Scenario()));
    ASSERT_NE(session.Rig(), nullptr);
    EXPECT_TRUE(FactBool(session, "Grounded"));

    session.SetFact("Speed", AnimationScenarioValue::FromNumber(4.0));
    ASSERT_EQ(session.Scenario().Actions.size(), 1u);
    EXPECT_EQ(session.Scenario().Actions[0].Tick, 1u);
    EXPECT_FLOAT_EQ(FactFloat(session, "Speed"), 0.0f); // nothing committed yet

    // The disposable next-tick evaluation shows the edit and its derived
    // consequence without advancing time.
    const std::vector<std::uint32_t> next = session.PreviewNextTick();
    EXPECT_FLOAT_EQ(AnimFactToFloat(next[static_cast<std::size_t>(session.Rig()->FindSlot("Speed"))]), 4.0f);
    EXPECT_TRUE(AnimFactToBool(next[static_cast<std::size_t>(session.Rig()->FindSlot("Moving"))]));
    EXPECT_EQ(session.Tick(), 0u);
    EXPECT_FALSE(FactBool(session, "Moving"));

    session.Step();
    EXPECT_EQ(session.Tick(), 1u);
    EXPECT_FLOAT_EQ(FactFloat(session, "Speed"), 4.0f);
    EXPECT_TRUE(FactBool(session, "Moving"));
}

TEST(AnimationPreviewSession, DerivedHistoryShowsTheLanding)
{
    PreviewFixture fx;
    AnimationPreviewSession session(fx.Data, &PreviewFixture::Vocabulary);
    ASSERT_TRUE(session.Open(PreviewFixture::Scenario()));

    session.SetFact("Grounded", AnimationScenarioValue::FromBool(false));
    for (int i = 0; i < 10; ++i)
        session.Step();
    EXPECT_TRUE(FactBool(session, "Airborne"));
    session.SetFact("Grounded", AnimationScenarioValue::FromBool(true));
    session.Step();
    EXPECT_TRUE(FactBool(session, "JustLanded"));
    EXPECT_FALSE(FactBool(session, "Airborne"));

    // History keeps every tick's snapshot, so a panel can plot the edge.
    const int justLanded = session.Rig()->FindSlot("JustLanded");
    std::size_t landedTicks = 0;
    for (const AnimationPreviewTickRecord& record : session.History())
        landedTicks += AnimFactToBool(record.Facts[static_cast<std::size_t>(justLanded)]) ? 1 : 0;
    EXPECT_EQ(landedTicks, 1u);
    EXPECT_EQ(session.History().size(), 12u);
}

TEST(AnimationPreviewSession, RequestConsoleShowsCapacityPrimaryAndRetention)
{
    PreviewFixture fx;
    AnimationScenario scenario = PreviewFixture::Scenario();
    for (int i = 0; i < 8; ++i)
        scenario.Participants.push_back("p" + std::to_string(i));
    AnimationPreviewSession session(fx.Data, &PreviewFixture::Vocabulary);
    ASSERT_TRUE(session.Open(scenario));

    // Ten held requests from ten sources: eight fit, and the rest are refused
    // without anything being evicted.
    for (const std::string& participant : session.Scenario().Participants)
        session.IssueRequest(Issue(participant, "anim.intent.reload"));
    session.Step();
    const AnimationPreviewTickRecord& record = session.History().back();
    ASSERT_EQ(record.Actions.size(), 10u);
    int rejected = 0;
    for (const AnimationPreviewActionOutcome& outcome : record.Actions)
        rejected += outcome.Request.Reject == AnimRejectReason::Capacity ? 1 : 0;
    EXPECT_EQ(rejected, 2);
    EXPECT_EQ(record.Requests.size(), kAnimRequestCapacity);

    // Primary: all started on one tick, so the highest sequence.
    const GameplayTagId reload = session.Rig()->Intents[0].Intent;
    const AnimRequest* primary = FindPrimaryAnimRequest(*session.Requests(), reload, session.Tick());
    ASSERT_NE(primary, nullptr);
    EXPECT_EQ(primary->Id.Sequence, 8u);

    // Cancel is visible for its tick; the record is then pruned on the next
    // insert.
    session.CancelRequest("player", "anim.intent.reload", AnimCancelReason::Interrupted);
    session.Step();
    EXPECT_TRUE(session.History().back().Actions[0].Cancelled);
    EXPECT_EQ(FindAnimCancelReason(*session.Requests(), reload, session.Tick()),
              AnimCancelReason::Interrupted);
    session.IssueRequest(Issue("player", "anim.intent.flinch", AnimRequestLifetime::Impulse));
    session.Step();
    EXPECT_EQ(session.History().back().Actions[0].Request.Status, AnimRequestStatus::Accepted);
    EXPECT_EQ(session.History().back().Requests.size(), kAnimRequestCapacity);
}

TEST(AnimationPreviewSession, ASavedScenarioReproducesTheTake)
{
    PreviewFixture fx;
    AnimationPreviewSession live(fx.Data, &PreviewFixture::Vocabulary);
    ASSERT_TRUE(live.Open(PreviewFixture::Scenario()));

    live.SetFact("Speed", AnimationScenarioValue::FromNumber(3.0));
    live.SetFact("Stance", AnimationScenarioValue::FromName("stance.crouch"));
    live.Step();
    AnimationScenarioAction reload = Issue("player", "anim.intent.reload");
    reload.Params = { { "rate", AnimationScenarioValue::FromNumber(1.5) } };
    live.IssueRequest(reload);
    live.Step();
    live.Step();
    live.SetFact("Grounded", AnimationScenarioValue::FromBool(false));
    live.IssueRequest(Issue("ai", "anim.intent.flinch", AnimRequestLifetime::Impulse));
    for (int i = 0; i < 6; ++i)
        live.Step();
    live.CancelRequest("player", "anim.intent.reload", AnimCancelReason::Released);
    live.SetFact("Grounded", AnimationScenarioValue::FromBool(true));
    for (int i = 0; i < 4; ++i)
        live.Step();
    EXPECT_TRUE(live.ScenarioModified());

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "sencha_preview_take.sanimscenario";
    std::string error;
    ASSERT_TRUE(SaveAnimationScenario(live.Scenario(), path.string(), error)) << error;
    live.MarkScenarioSaved();
    EXPECT_FALSE(live.ScenarioModified());

    std::vector<AnimDiagnostic> diagnostics;
    std::optional<AnimationScenario> loaded = LoadAnimationScenario(path.string(), diagnostics);
    std::filesystem::remove(path);
    ASSERT_TRUE(loaded.has_value());
    ASSERT_TRUE(diagnostics.empty());

    AnimationPreviewSession replay(fx.Data, &PreviewFixture::Vocabulary);
    ASSERT_TRUE(replay.Open(std::move(*loaded)));
    replay.RunTo(live.Tick());

    ASSERT_EQ(replay.History().size(), live.History().size());
    for (std::size_t i = 0; i < live.History().size(); ++i)
        EXPECT_TRUE(SameAnimationPreviewTick(live.History()[i], replay.History()[i])) << "tick " << i;
    EXPECT_TRUE(replay.ScenarioProblems().empty());

    // And restarting the live session is the same replay.
    const AnimationPreviewTickRecord last = live.History().back();
    live.RunTo(0);
    live.RunTo(last.Tick);
    EXPECT_TRUE(SameAnimationPreviewTick(live.History().back(), last));
}

TEST(AnimationPreviewSession, ActingInsideAScriptedRunBranchesIt)
{
    PreviewFixture fx;
    AnimationScenario scenario = PreviewFixture::Scenario();
    AnimationScenarioAction late;
    late.Tick = 10;
    late.Kind = AnimationScenarioActionKind::SetFact;
    late.Fact = "Speed";
    late.Value = AnimationScenarioValue::FromNumber(9.0);
    scenario.Append(late);

    AnimationPreviewSession session(fx.Data, &PreviewFixture::Vocabulary);
    ASSERT_TRUE(session.Open(scenario));
    session.RunTo(3);
    session.SetFact("Speed", AnimationScenarioValue::FromNumber(1.0));
    session.SetFact("Dead", AnimationScenarioValue::FromBool(true));

    // Both edits made while paused on tick 3 survive; the scripted tick-10
    // action does not.
    ASSERT_EQ(session.Scenario().Actions.size(), 2u);
    EXPECT_EQ(session.Scenario().Actions[0].Tick, 4u);
    EXPECT_EQ(session.Scenario().Actions[1].Tick, 4u);
    session.RunTo(12);
    EXPECT_FLOAT_EQ(FactFloat(session, "Speed"), 1.0f);
}

TEST(AnimationPreviewSession, UnknownNamesAreProblemsNotRegistrations)
{
    PreviewFixture fx;
    AnimationPreviewSession session(fx.Data, &PreviewFixture::Vocabulary);
    ASSERT_TRUE(session.Open(PreviewFixture::Scenario()));

    session.SetFact("Sped", AnimationScenarioValue::FromNumber(3.0));
    session.SetFact("Moving", AnimationScenarioValue::FromBool(true));
    session.SetFact("Stance", AnimationScenarioValue::FromName("stance.crawl"));
    session.SetFact("Speed", AnimationScenarioValue::FromName("fast"));
    session.IssueRequest(Issue("player", "anim.intent.relaod"));
    AnimationScenarioAction badParam = Issue("player", "anim.intent.reload");
    badParam.Params = { { "speed", AnimationScenarioValue::FromNumber(2.0) } };
    session.IssueRequest(badParam);
    session.Step();

    EXPECT_TRUE(HasProblem(session, "anim.scenario.unknown_fact", "$.actions[0].set"));
    EXPECT_TRUE(HasProblem(session, "anim.scenario.derived_fact", "$.actions[1].set"));
    EXPECT_TRUE(HasProblem(session, "anim.scenario.unknown_tag", "$.actions[2].set"));
    EXPECT_TRUE(HasProblem(session, "anim.scenario.value_kind", "$.actions[3].set"));
    EXPECT_TRUE(HasProblem(session, "anim.scenario.unknown_intent", "$.actions[4].issue"));
    EXPECT_TRUE(HasProblem(session, "anim.scenario.unknown_param", "$.actions[5].params.speed"));
    EXPECT_EQ(session.Input("Stance"), nullptr);
    EXPECT_FALSE(FactBool(session, "Moving"));
}

TEST(AnimationPreviewSession, PreviewNeverTouchesTheAssetsItRuns)
{
    PreviewFixture fx;
    const DataAssetHandle rig = fx.Data.Find(kRig);
    const std::uint64_t version = fx.Data.GetReloadVersion(rig);
    const void* value = fx.Data.GetRaw(rig);

    AnimationPreviewSession session(fx.Data, &PreviewFixture::Vocabulary);
    ASSERT_TRUE(session.Open(PreviewFixture::Scenario()));
    session.SetFact("Speed", AnimationScenarioValue::FromNumber(3.0));
    session.IssueRequest(Issue("player", "anim.intent.reload"));
    session.RunTo(30);

    EXPECT_EQ(fx.Data.GetReloadVersion(rig), version);
    EXPECT_EQ(fx.Data.GetRaw(rig), value);
}

TEST(AnimationPreviewSession, SpeedSchedulesTicksWithoutChangingThem)
{
    PreviewFixture fx;
    AnimationPreviewSession session(fx.Data, &PreviewFixture::Vocabulary);
    ASSERT_TRUE(session.Open(PreviewFixture::Scenario()));
    session.Play();
    session.Advance(0.05); // three ticks at 60 Hz
    EXPECT_EQ(session.Tick(), 3u);
    ASSERT_TRUE(session.SetSpeed(2.0));
    session.Advance(0.05);
    EXPECT_EQ(session.Tick(), 9u);
    EXPECT_DOUBLE_EQ(session.TickSeconds(), 1.0 / 60.0);
    EXPECT_FALSE(session.SetSpeed(0.0));
}

TEST(AnimationPreviewSession, AnUnloadedRigIsAProblem)
{
    PreviewFixture fx;
    AnimationScenario scenario = PreviewFixture::Scenario();
    scenario.RigPath = "asset://animation/missing.rig.sdata";
    AnimationPreviewSession session(fx.Data, &PreviewFixture::Vocabulary);
    EXPECT_FALSE(session.Open(scenario));
    EXPECT_TRUE(HasProblem(session, "anim.scenario.rig_unavailable", "$.rig"));
    session.Step(); // harmless
    EXPECT_EQ(session.Tick(), 0u);
}

TEST(AnimationPreviewSession, DeclaredFixtureTagsStandInForAGameModule)
{
    PreviewFixture fx;
    fx.Load("asset://animation/dash.requests.sdata", kAnimRequestSchemaType, R"({
        "intents": [ { "intent": "game.intent.dash", "params": [] } ] })");
    fx.Load("asset://animation/dash.rig.sdata", kAnimRigType, R"({
        "requests": "asset://animation/dash.requests.sdata",
        "layers": [ { "name": "anim.layer.base" } ] })");

    AnimationScenario scenario;
    scenario.RigPath = "asset://animation/dash.rig.sdata";
    scenario.Participants = { "player" };

    // Without a module or a fixture the intent names nothing, and the rig says so.
    AnimationPreviewSession bare(fx.Data);
    ASSERT_TRUE(bare.Open(scenario));
    EXPECT_FALSE(bare.Rig()->Valid);

    scenario.DeclaredTags = { "game.intent.dash" };
    AnimationPreviewSession declared(fx.Data);
    ASSERT_TRUE(declared.Open(scenario));
    EXPECT_TRUE(declared.Rig()->Valid);
    declared.IssueRequest(Issue("player", "game.intent.dash", AnimRequestLifetime::Impulse));
    declared.Step();
    EXPECT_EQ(declared.History().back().Actions[0].Request.Status, AnimRequestStatus::Accepted);

    std::vector<AnimDiagnostic> diagnostics;
    const std::optional<AnimationScenario> reread =
        ReadAnimationScenario(WriteAnimationScenario(declared.Scenario()), "dash", diagnostics);
    ASSERT_TRUE(reread.has_value());
    EXPECT_EQ(reread->DeclaredTags, scenario.DeclaredTags);
}

TEST(AnimationRigOutline, ListsTheChainAndWhatIsMissing)
{
    PreviewFixture fx;
    const std::vector<AnimationRigDependency> rows = DescribeAnimationRigDependencies(fx.Data, kRig);
    ASSERT_EQ(rows.size(), 4u);
    EXPECT_EQ(rows[0].Role, "Rig");
    EXPECT_EQ(rows[1].Path, "asset://animation/hero.facts.sdata");
    EXPECT_EQ(rows[2].Role, "Extends");
    EXPECT_EQ(rows[2].Depth, 2);
    EXPECT_EQ(rows[3].Role, "Requests");
    for (const AnimationRigDependency& row : rows)
        EXPECT_EQ(row.Status, AnimationRigDependency::State::Resident) << row.Path;

    fx.Load("asset://animation/broken.rig.sdata", kAnimRigType, R"({
        "facts": "asset://animation/absent.facts.sdata",
        "requests": "asset://animation/engine.facts.sdata",
        "layers": [ { "name": "anim.layer.base" } ] })");
    const std::vector<AnimationRigDependency> broken =
        DescribeAnimationRigDependencies(fx.Data, "asset://animation/broken.rig.sdata");
    ASSERT_EQ(broken.size(), 3u);
    EXPECT_EQ(broken[1].Status, AnimationRigDependency::State::Missing);
    EXPECT_EQ(broken[2].Status, AnimationRigDependency::State::WrongSubtype);
}

// The shipped fixture: a rig over the engine's fact schema, loaded through the
// runtime asset pipeline, with the scenario saved beside it. Its request
// intents are game tags, declared by the scenario as preview fixtures.
TEST(AnimationPreviewSession, TheFixtureScenarioRunsCleanThroughTheAssetPipeline)
{
    const std::filesystem::path repo(SENCHA_REPO_ROOT);
    LoggingProvider logging;
    ComponentSerializerRegistry serializers;
    RuntimeAssets assets(logging, serializers);
    for (const std::filesystem::path& root : { repo / "engine/assets", repo / "engine/assets/.cooked",
                                              repo / "test/fixtures/content/assets",
                                              repo / "test/fixtures/content/assets/.cooked" })
        (void)ScanAssetsDirectory(root.generic_string(), assets.Registry, assets.Assets.Kinds());

    AssetLease rig = assets.Assets.LoadLease("asset://animation/hero.rig.sdata", AssetType::Data);
    ASSERT_TRUE(rig);

    std::vector<AnimDiagnostic> diagnostics;
    std::optional<AnimationScenario> scenario = LoadAnimationScenario(
        (repo / "test/fixtures/content/assets/animation/hero.rig.sanimscenario").string(), diagnostics);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_TRUE(diagnostics.empty()) << FormatAnimDiagnostic(diagnostics.front());

    AnimationPreviewSession session(assets.DataAssets);
    ASSERT_TRUE(session.Open(*scenario));
    ASSERT_TRUE(session.Rig()->Valid) << FormatAnimDiagnostic(session.Rig()->Diagnostics.front());
    EXPECT_EQ(session.Rig()->Slots.front().DeclaredIn, "asset://animation/engine.facts.sdata");
    session.RunTo(180);
    EXPECT_TRUE(session.ScenarioProblems().empty())
        << FormatAnimDiagnostic(session.ScenarioProblems().front());
    EXPECT_TRUE(session.FactsExact());

    const AnimationPreviewTickRecord last = session.History().back();
    AnimationPreviewSession replay(assets.DataAssets);
    ASSERT_TRUE(replay.Open(std::move(*scenario)));
    replay.RunTo(180);
    EXPECT_TRUE(SameAnimationPreviewTick(replay.History().back(), last));
    session.Close();
    replay.Close();
}
