// Headless selection editing: selector edits as the rule table applies them,
// predicates as panels read them, and rule edits that change the running
// preview's winner and clip without writing to disk.

#include "authoring/AnimationPredicateEdits.h"
#include "authoring/AnimationPredicateText.h"
#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationSelectorEdits.h"

#include <anim/AnimSelectorData.h>
#include <anim/AnimationClipCache.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/json/JsonFormat.h>
#include <core/json/JsonParser.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
    JsonValue Parse(std::string_view text)
    {
        std::optional<JsonValue> value = JsonParse(text);
        EXPECT_TRUE(value.has_value()) << text;
        return value.value_or(JsonValue());
    }

    AnimPredicateDecl ReadRows(const JsonValue& rows)
    {
        AnimPredicateDecl decl;
        std::string error;
        EXPECT_TRUE(ReadAnimPredicate(&rows, "$", decl, error)) << error;
        return decl;
    }
}

TEST(AnimationPredicateText, ReadsTheWayItWasAuthored)
{
    const JsonValue rows = Parse(R"([
        { "fact": "Grounded", "not": true },
        { "fact": "Speed", "compare": "gt", "value": 2.2 },
        { "any": [ { "fact": "Stance", "compare": "eq", "tag": "stance.crouch" },
                   { "request": "anim.intent.reload", "test": "age", "compare": "lt", "value": 0.5 } ] },
        { "fact": "Tags", "has": "none", "query": [ "state.stunned", "state.dead" ] },
        { "elapsed": "behavior", "compare": "ge", "value": 1 } ])");
    EXPECT_EQ(DescribeAnimPredicate(ReadRows(rows)),
              "not Grounded and Speed > 2.2 and (Stance == stance.crouch or age of anim.intent.reload < 0.5s) "
              "and Tags has none of [state.stunned, state.dead] and time in behavior >= 1s");
    EXPECT_EQ(DescribeAnimPredicate(ReadRows(Parse("[]"))), "always");

    // A working document mid-edit reads as its problem instead of crashing.
    const JsonValue broken = Parse(R"([ { "fact": "Speed", "compare": "greater", "value": 1 } ])");
    EXPECT_EQ(DescribeAnimPredicate(&broken).rfind("invalid: $[0].compare", 0), 0u);
}

TEST(AnimationSelectorEdits, EachEditLeavesAValidSelector)
{
    DataAssetTypeRegistry types;
    DataSchemaRegistry schemas;
    RegisterAnimSelectorData(types, schemas);
    JsonValue root = Parse(R"({ "type": "animation.selector", "version": 1, "data": { "rules": [] } })");
    const auto compiles = [&] {
        const DataAssetCompileResult result = types.Find(kAnimSelectorType)->Compile(*root.Find("data"));
        EXPECT_TRUE(result.IsValid()) << result.Error;
        std::vector<DataValidationError> errors;
        EXPECT_TRUE(ValidateDataAgainstSchema(*root.Find("data"), *schemas.Find(kAnimSelectorType), errors))
            << (errors.empty() ? "" : errors.front().Path + " " + errors.front().Message);
        return result.IsValid() ? std::static_pointer_cast<const AnimSelectorData>(result.Value) : nullptr;
    };

    AddAnimSelectorRule(root, "walk", "Anim.Walk", 10);
    AddAnimSelectorRule(root, "idle", "Anim.Idle", 0);
    const auto enter = [&](std::size_t rule) { return AnimSelectorPredicate(root, rule, "enter"); };
    ASSERT_NE(enter(0), nullptr);
    AddAnimPredicateRow(*enter(0), MakeAnimFactTest("Speed", AnimFactKind::Float));
    AddAnimPredicateRow(*enter(0), MakeAnimFactTest("Grounded", AnimFactKind::Bool));
    ASSERT_TRUE(AddAnimPredicateAlternative(*enter(0), 1, MakeAnimRequestTest("anim.intent.fly")));
    ASSERT_TRUE(SetAnimSelectorStay(root, 0, true));
    std::shared_ptr<const AnimSelectorData> selector = compiles();
    ASSERT_NE(selector, nullptr);
    ASSERT_EQ(selector->Rules[0].Enter.Rows.size(), 2u);
    EXPECT_EQ(selector->Rules[0].Enter.Rows[1].AnyOf.size(), 2u);
    EXPECT_TRUE(selector->Rules[0].HasStay);
    EXPECT_EQ(DescribeAnimPredicate(selector->Rules[0].Stay),
              "Speed > 0 and (Grounded or request anim.intent.fly)");

    // Back out of the group: the remaining test stands alone again.
    ASSERT_TRUE(RemoveAnimPredicateAlternative(*enter(0), 1, 1));
    ASSERT_TRUE(SetAnimSelectorStay(root, 0, false));
    ASSERT_TRUE(MoveAnimSelectorRule(root, 1, 0));
    selector = compiles();
    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->Rules[0].Name, "idle");
    EXPECT_EQ(DescribeAnimPredicate(selector->Rules[1].Enter), "Speed > 0 and Grounded");
    EXPECT_FALSE(selector->Rules[1].HasStay);
    ASSERT_TRUE(RemoveAnimSelectorRule(root, 0));
    EXPECT_EQ(compiles()->Rules.size(), 1u);
    EXPECT_EQ(AnimSelectorPredicate(root, 5, "enter"), nullptr);
    EXPECT_FALSE(RemoveAnimPredicateRow(*enter(0), 7));
}

namespace
{
    // A project on disk, loaded the way the editor loads one: sdata files
    // scanned from a content root, clips supplied procedurally so no cook is
    // needed.
    struct Project
    {
        std::filesystem::path Root = std::filesystem::temp_directory_path() / "sencha_selection_editing";
        LoggingProvider Logging;
        ComponentSerializerRegistry Serializers;
        std::unique_ptr<RuntimeAssets> Assets;
        std::vector<AssetLease> ClipLeases;

        Project()
        {
            std::filesystem::remove_all(Root);
            std::filesystem::create_directories(Root / "anim");
            Write("hero.facts.sdata", R"({ "type": "animation.fact_schema", "version": 1, "data": {
                "slots": [ { "name": "Speed", "kind": "float" } ] } })");
            Write("hero.behaviors.sdata", R"({ "type": "animation.behavior_set", "version": 1, "data": {
                "behaviors": [ { "tag": "Anim.Idle", "kind": "cyclic" }, { "tag": "Anim.Walk", "kind": "cyclic" } ] } })");
            Write("hero.selector.sdata", R"({ "type": "animation.selector", "version": 1, "data": { "rules": [
                { "name": "walk", "priority": 10, "enter": [ { "fact": "Speed", "compare": "gt", "value": 0.1 } ],
                  "behavior": "Anim.Walk" },
                { "name": "idle", "priority": 0, "enter": [], "behavior": "Anim.Idle" } ] } })");
            Write("hero.slots.sdata", R"({ "type": "animation.slot_map", "version": 1, "data": { "rows": [
                { "id": "idle", "behavior": "Anim.Idle", "clip": "asset://anim/idle.sanim" },
                { "id": "walk", "behavior": "Anim.Walk", "clip": "asset://anim/walk.sanim" } ] } })");
            Write("hero.rig.sdata", R"({ "type": "animation.rig", "version": 1, "data": {
                "facts": "asset://anim/hero.facts.sdata", "behaviors": [ "asset://anim/hero.behaviors.sdata" ],
                "slot_maps": [ "asset://anim/hero.slots.sdata" ],
                "layers": [ { "name": "anim.layer.base", "selector": "asset://anim/hero.selector.sdata",
                              "idle": "Anim.Idle" } ] } })");
            Write("hero.rig.sanimscenario", R"({ "type": "animation.preview_scenario", "version": 1,
                "name": "walk", "rig": "asset://anim/hero.rig.sdata", "participants": [ "player" ],
                "declared_tags": [ "Anim.Idle", "Anim.Walk" ], "inputs": { "Speed": 1.0 } })");

            Assets = std::make_unique<RuntimeAssets>(Logging, Serializers);
            for (const char* clip : { "asset://anim/idle.sanim", "asset://anim/walk.sanim" })
            {
                EXPECT_TRUE(Assets->Registry.RegisterOrVerify(AssetRecord{
                    .Type = AssetType::AnimationClip, .SourceKind = AssetSourceKind::Procedural, .Path = clip }));
                AnimationClipData data;
                data.DurationSeconds = 1.0f;
                (void)Assets->AnimationClips.Register(clip, std::move(data), {});
                ClipLeases.push_back(Assets->Assets.TryAcquireLease(clip, AssetType::AnimationClip));
            }
            (void)ScanAssetsDirectory(Root.generic_string(), Assets->Registry, Assets->Assets.Kinds());
        }

        ~Project()
        {
            ClipLeases.clear();
            std::filesystem::remove_all(Root);
        }

        void Write(const std::string& name, std::string_view text)
        {
            std::ofstream(Root / "anim" / name) << text;
        }

        std::string Read(const std::string& name) const
        {
            std::ifstream in(Root / "anim" / name);
            std::stringstream text;
            text << in.rdbuf();
            return text.str();
        }
    };

    std::string Playing(AnimationPreviewWorkspace& workspace)
    {
        const AnimBoundRig* rig = workspace.Rig.Simulation.Rig();
        const AnimContentState* content = workspace.Rig.Simulation.Content();
        if (rig == nullptr || content == nullptr || content->Layers[0].Content >= rig->Contents.size())
            return "(none)";
        return rig->Contents[content->Layers[0].Content].Path;
    }
}

TEST(AnimationSelectionEditing, EditingARuleChangesTheRunningPreview)
{
    Project project;
    {
        AnimationPreviewWorkspace workspace(*project.Assets);
        ASSERT_TRUE(workspace.OpenRig("asset://anim/hero.rig.sdata")) << workspace.Rig.Error;
        ASSERT_NE(workspace.Rig.Simulation.Rig(), nullptr);
        ASSERT_TRUE(workspace.Rig.Simulation.Rig()->Valid);
        EXPECT_EQ(Playing(workspace), "asset://anim/walk.sanim");

        // Raise walk's threshold above the scenario's speed, through the same
        // edit and transaction the rule table uses.
        ASSERT_TRUE((workspace.Documents.OpenOrFocus("asset://anim/hero.selector.sdata", workspace.DocumentError) != nullptr));
        DataDocument* selector = workspace.Documents.ActiveOf(kAnimSelectorType);
        ASSERT_NE(selector, nullptr);
        const std::string saved = project.Read("hero.selector.sdata");
        JsonValue root = selector->CopyRoot();
        (*AnimSelectorRules(root))[0].Find("enter")->AsArray()[0] =
            Parse(R"({ "fact": "Speed", "compare": "gt", "value": 5 })");
        selector->BeginEdit();
        selector->PreviewRoot(std::move(root));
        workspace.Documents.Store().CommitEdit(*selector);
        EXPECT_EQ(workspace.Documents.Store().ResidentStateOf(*selector)->Status, DataResidentStatus::Current);

        // The next tick decides with the edited rule; nothing restarted.
        const AnimTick before = workspace.Rig.Simulation.Tick();
        workspace.Rig.Simulation.Step();
        EXPECT_EQ(workspace.Rig.Simulation.Tick(), before + 1);
        EXPECT_EQ(Playing(workspace), "asset://anim/idle.sanim");

        // An invalid working edit leaves the preview on the last valid one.
        root = selector->CopyRoot();
        (*AnimSelectorRules(root))[0].Find("enter")->AsArray()[0] = Parse(R"({ "fact": "Speed", "compare": "gt" })");
        (*AnimSelectorRules(root))[0].Find("enter")->AsArray()[0].AsObject().emplace_back("value", JsonValue("fast"));
        selector->BeginEdit();
        selector->PreviewRoot(std::move(root));
        workspace.Documents.Store().CommitEdit(*selector);
        EXPECT_EQ(workspace.Documents.Store().ResidentStateOf(*selector)->Status, DataResidentStatus::KeptLastValid);
        workspace.Rig.Simulation.Step();
        EXPECT_EQ(Playing(workspace), "asset://anim/idle.sanim");

        // Undo twice: back to the authored rule, and the preview follows.
        selector->Undo();
        workspace.Documents.Store().Changed(*selector);
        selector->Undo();
        workspace.Documents.Store().Changed(*selector);
        workspace.Rig.Simulation.Step();
        EXPECT_EQ(Playing(workspace), "asset://anim/walk.sanim");

        // None of it reached the file.
        EXPECT_EQ(project.Read("hero.selector.sdata"), saved);
        EXPECT_FALSE(selector->IsDirty());
        workspace.Rig.Simulation.Close();
    }
}

TEST(AnimationSelectionEditing, TheSessionRecordsWhyEveryRuleLost)
{
    Project project;
    AnimationPreviewWorkspace workspace(*project.Assets);
    ASSERT_TRUE(workspace.OpenRig("asset://anim/hero.rig.sdata")) << workspace.Rig.Error;
    workspace.Rig.Simulation.SetFact("Speed", AnimationScenarioValue::FromNumber(0.0));

    // Before stepping, the next tick is explained from disposable copies.
    const std::vector<std::vector<AnimRuleVerdict>> next = workspace.Rig.Simulation.ExplainNextTick();
    ASSERT_EQ(next.size(), 1u);
    ASSERT_EQ(next[0].size(), 2u);
    EXPECT_EQ(next[0][0].Kind, AnimRuleVerdictKind::Failed);
    EXPECT_TRUE(next[0][0].EvaluatedStay);
    EXPECT_EQ(next[0][1].Kind, AnimRuleVerdictKind::Winner);
    EXPECT_EQ(Playing(workspace), "asset://anim/walk.sanim");

    workspace.Rig.Simulation.Step();
    const AnimationPreviewTickRecord& record = workspace.Rig.Simulation.History().back();
    ASSERT_EQ(record.Layers.size(), 1u);
    EXPECT_EQ(record.Layers[0].Winner, 1u);
    const AnimBoundRule& walk = workspace.Rig.Simulation.Rig()->Selectors[0].Rules[0];
    EXPECT_EQ(DescribeAnimVerdict(workspace.DataCache(), walk, record.Layers[0].Verdicts[0]),
              "stay failed: Speed > 0.1 (read 0)");
    workspace.Rig.Simulation.Close();
}
