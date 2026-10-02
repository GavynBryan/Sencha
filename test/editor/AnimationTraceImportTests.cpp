// A trace a game exported reads back in the editor as the game logged it,
// and says what it does not hold.

#include "EditorDocumentsFixture.h"
#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationTraceImport.h"
#include "authoring/AnimationTraceProperties.h"

#include "AnimationTestProject.h"

#include <anim/AnimDecisionLog.h>
#include <anim/AnimTrace.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <gtest/gtest.h>

#include <format>
#include <fstream>

// What the engine writes, the editor reads: a full ring, so the earliest
// records are gone and the reader says how many.
TEST(AnimationTraceImport, ReadsWhatTheGameWrote)
{
    World world;
    world.RegisterComponent<AnimDecisionLog>();
    GameplayTagRegistry& tags = world.AddResource<GameplayTagRegistry>();
    const GameplayTagId reload = *tags.RegisterTag("anim.intent.reload");
    const EntityId entity = world.CreateEntity();
    world.AddComponent(entity, AnimDecisionLog{});
    AnimDecisionLog& log = *world.TryGet<AnimDecisionLog>(entity);
    constexpr std::uint64_t kLogged = kAnimDecisionLogCapacity + 6;
    for (std::uint64_t tick = 0; tick < kLogged; ++tick)
    {
        AnimDecisionRecord record;
        record.Tick = tick;
        record.Cause = AnimDecisionCause::RequestAdded;
        record.Intent = reload;
        log.Append(record);
    }

    std::string error;
    const std::optional<AnimationTrace> trace =
        ReadAnimationTrace(WriteAnimTrace(world, entity, nullptr), error);
    ASSERT_TRUE(trace.has_value()) << error;
    EXPECT_EQ(trace->Entity, std::format("{}:{}", entity.Index, entity.Generation));
    EXPECT_TRUE(trace->Rig.empty());
    EXPECT_EQ(trace->Captured, "decisions");
    ASSERT_EQ(trace->Rows.size(), kAnimDecisionLogCapacity);
    EXPECT_EQ(trace->Overwritten(), 6u);
    EXPECT_EQ(trace->Rows.front().Tick, 6u);
    EXPECT_EQ(trace->Rows.front().Cause, AnimDecisionCauseName(AnimDecisionCause::RequestAdded));
    EXPECT_NE(trace->Rows.front().Detail.find("intent anim.intent.reload"), std::string::npos)
        << trace->Rows.front().Detail;
}

// A field this editor does not know is shown, not dropped.
TEST(AnimationTraceImport, UnknownFieldsAreShown)
{
    std::string error;
    const std::optional<AnimationTrace> trace = ReadAnimationTrace(*JsonParse(R"({
        "type": "animation.trace", "version": 1, "entity": "3:1", "rig": "asset://anim/hero.rig.sdata",
        "captured": "decisions", "records_written": 1,
        "records": [ { "tick": 12, "cause": "behavior_entered", "layer": 0, "layer_name": "anim.layer.base",
                       "behavior": "Anim.Locomotion.Walk", "future_field": { "weight": 0.5 } } ] })"),
                                                                   error);
    ASSERT_TRUE(trace.has_value()) << error;
    ASSERT_EQ(trace->Rows.size(), 1u);
    EXPECT_EQ(trace->Rows[0].Tick, 12u);
    EXPECT_EQ(trace->Rows[0].Layer, "anim.layer.base");
    EXPECT_EQ(trace->Rows[0].Detail, "behavior Anim.Locomotion.Walk, future_field (weight 0.5)");
    EXPECT_EQ(trace->Overwritten(), 0u);
}

TEST(AnimationTraceImport, OtherDocumentsAreRefused)
{
    std::string error;
    EXPECT_FALSE(ReadAnimationTrace(*JsonParse(R"({ "type": "animation.rig", "version": 1, "records": [] })"),
                                    error));
    EXPECT_NE(error.find("not an animation trace"), std::string::npos);
    EXPECT_FALSE(ReadAnimationTrace(*JsonParse(R"({ "type": "animation.trace", "version": 2, "records": [] })"),
                                    error));
    EXPECT_NE(error.find("version"), std::string::npos);
    EXPECT_FALSE(ReadAnimationTraceFile("/nonexistent/trace.json", error));
}

// As a sheet: a trace file read, its records filtered, and a file that is not
// a trace refused with why.
TEST(AnimationTraceImport, TheSheetReadsAndFiltersAFile)
{
    World world;
    world.RegisterComponent<AnimDecisionLog>();
    GameplayTagRegistry& tags = world.AddResource<GameplayTagRegistry>();
    const GameplayTagId reload = *tags.RegisterTag("anim.intent.reload");
    const EntityId entity = world.CreateEntity();
    world.AddComponent(entity, AnimDecisionLog{});
    AnimDecisionLog& log = *world.TryGet<AnimDecisionLog>(entity);
    for (std::uint64_t tick = 0; tick < 5; ++tick)
    {
        AnimDecisionRecord record;
        record.Tick = tick;
        record.Cause = tick % 2 == 0 ? AnimDecisionCause::RequestAdded : AnimDecisionCause::BehaviorEntered;
        record.Intent = reload;
        log.Append(record);
    }
    AnimationTestProject project("sencha_trace_sheet");
    const std::filesystem::path file = project.Root / "trace.json";
    std::ofstream(file) << JsonStringify(WriteAnimTrace(world, entity, nullptr));

    EditorDocuments documents(*project.Assets);
    AnimationPreviewWorkspace workspace(*project.Assets, documents.Sources, documents.Store, {}, project.Root);
    AnimationTraceProperties sheet(workspace);
    ASSERT_TRUE(sheet.BeginEdit("file"));
    EXPECT_EQ(sheet.CommitEdit("file", (project.Root / "missing.json").string()), PropertyEditResult::Applied);
    EXPECT_EQ(sheet.Act("file", "read", ""), PropertyEditResult::Refused);
    std::vector<PropertyNode> nodes = sheet.Nodes();
    EXPECT_NE(std::ranges::find(nodes, std::string("error"), &PropertyNode::Key), nodes.end());

    EXPECT_EQ(sheet.CommitEdit("file", file.string()), PropertyEditResult::Applied);
    EXPECT_EQ(sheet.Act("file", "read", ""), PropertyEditResult::Applied);
    nodes = sheet.Nodes();
    auto rows = std::ranges::find(nodes, std::string("rows"), &PropertyNode::Key);
    ASSERT_NE(rows, nodes.end());
    EXPECT_EQ(rows->Children.size(), 5u);
    EXPECT_EQ(sheet.CommitEdit("filter", std::string(AnimDecisionCauseName(AnimDecisionCause::BehaviorEntered))),
              PropertyEditResult::Applied);
    nodes = sheet.Nodes();
    rows = std::ranges::find(nodes, std::string("rows"), &PropertyNode::Key);
    ASSERT_NE(rows, nodes.end());
    EXPECT_EQ(rows->Children.size(), 2u);
}
