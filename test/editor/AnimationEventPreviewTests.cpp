// Clip events in the animation preview: the production event pass, offered
// through a dispatcher whose only implementations are the recorders the
// scenario names. What the preview shows is the normal admission; a recorder's
// result is always labelled as one.

#include "authoring/AnimationClipPreviewSession.h"
#include "authoring/AnimationPreviewSession.h"
#include "authoring/AnimationScenario.h"

#include <anim/AnimBehaviorSet.h>
#include <anim/AnimRequestSchema.h>
#include <anim/AnimRigData.h>
#include <anim/AnimSlotMapData.h>
#include <anim/AnimationClipCache.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <authored/VerbBindingData.h>
#include <authored/VerbRegistry.h>
#include <authored/WorldVocabulary.h>
#include <core/json/JsonParser.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
    constexpr std::string_view kRig = "asset://animation/walker.rig.sdata";
    constexpr std::string_view kClip = "asset://animation/walk.sanim";
    constexpr std::string_view kBindingsPath = "asset://animation/walker.bindings.sdata";

    constexpr std::string_view kBindings = R"({ "bindings": [
        { "key": "anim.footstep", "verb": "test.footstep", "inputs": [ "surface", "volume" ],
          "arguments": { "Surface": { "input": "surface" }, "Volume": { "input": "volume" } } },
        { "key": "melee.hit", "verb": "test.hit", "arguments": {} } ] })";

    DataFieldSchema Arg(std::string key, DataFieldKind kind)
    {
        DataFieldSchema field;
        field.Key = std::move(key);
        field.Kind = kind;
        return field;
    }

    struct EventPreviewFixture
    {
        DataAssetTypeRegistry Types;
        DataSchemaRegistry Schemas;
        DataAssetCache Data;
        AnimationClipCache Clips;

        EventPreviewFixture()
        {
            RegisterAnimRequestSchema(Types, Schemas);
            RegisterAnimRigData(Types, Schemas);
            RegisterAnimBehaviorSet(Types, Schemas);
            RegisterAnimSlotMapData(Types, Schemas);
            RegisterVerbBindingData(Types, Schemas);

            // A cosmetic footstep a quarter in, and a gameplay hit halfway.
            AnimationClipData clip;
            clip.SkeletonPath = "asset://animation/walker.sskel";
            clip.DurationSeconds = 1.0f;
            AnimationJointTrack track;
            track.Path = AnimationChannelPath::Rotation;
            track.TimesSeconds = { 0.0f, 1.0f };
            track.Values = { 0, 0, 0, 1, 0, 0, 0, 1 };
            clip.Tracks.push_back(track);
            AnimationClipEvent step;
            step.Key = 1;
            step.Time = 0.25f;
            step.Binding = "anim.footstep";
            VerbBindingArgument surface;
            surface.Key = "surface";
            surface.Source = VerbArgumentSource::Tag;
            surface.Text = "Surface.Grass";
            VerbBindingArgument volume;
            volume.Key = "volume";
            volume.Literal = JsonValue(0.5);
            step.Inputs = { surface, volume };
            AnimationClipEvent hit;
            hit.Key = 2;
            hit.Time = 0.5f;
            hit.Binding = "melee.hit";
            hit.Scope = AnimEventScope::Gameplay;
            clip.Events = { step, hit };
            (void)Clips.Register(kClip, std::move(clip), {});

            Load(kBindingsPath, kVerbBindingsTypeName, kBindings);
            Load("asset://animation/walker.behaviors.sdata", kAnimBehaviorSetType,
                 R"({ "behaviors": [ { "tag": "Anim.Walk", "kind": "cyclic" } ] })");
            Load("asset://animation/walker.slots.sdata", kAnimSlotMapType,
                 R"({ "rows": [ { "behavior": "Anim.Walk", "clip": "asset://animation/walk.sanim" } ] })");
            Load(kRig, kAnimRigType, R"({
                "behaviors": [ "asset://animation/walker.behaviors.sdata" ],
                "slot_maps": [ "asset://animation/walker.slots.sdata" ],
                "bindings": [ "asset://animation/walker.bindings.sdata" ],
                "layers": [ { "name": "anim.layer.base", "idle": "Anim.Walk" } ] })");
        }

        void Load(std::string_view path, std::string_view type, std::string_view json)
        {
            const DataAssetCompileResult compiled = Types.Find(type)->Compile(*JsonParse(json));
            ASSERT_TRUE(compiled.IsValid()) << path << ": " << compiled.Error;
            (void)Data.Register(path, std::string(type), compiled.Value);
        }

        void Reload(std::string_view path, std::string_view type, std::string_view json)
        {
            const DataAssetCompileResult compiled = Types.Find(type)->Compile(*JsonParse(json));
            ASSERT_TRUE(compiled.IsValid()) << compiled.Error;
            ASSERT_TRUE(Data.ReloadInPlace(path, type, compiled.Value));
        }

        // What a project's module hook would declare.
        static void Vocabulary(World& world)
        {
            GameplayTagRegistry& tags = world.GetResource<GameplayTagRegistry>();
            for (const char* name : { "Anim.Walk", "Surface.Grass" })
                (void)tags.RegisterTag(name);
            VerbRegistry& verbs = InstallVerbRegistry(world);
            VerbRegistrationScope scope(verbs, "test");
            VerbDefinition footstep;
            footstep.Name = "test.footstep";
            footstep.Arguments.Children = { Arg("Surface", DataFieldKind::GameplayTag),
                                            Arg("Volume", DataFieldKind::Float) };
            (void)scope.Declare(std::move(footstep));
            VerbDefinition hit;
            hit.Name = "test.hit";
            (void)scope.Declare(std::move(hit));
            (void)scope.Commit();
        }

        static AnimationScenario Scenario()
        {
            AnimationScenario scenario;
            scenario.Name = "walk";
            scenario.RigPath = std::string(kRig);
            scenario.Participants = { "player" };
            return scenario;
        }
    };

    // Every event record in the session's history, oldest first.
    std::vector<AnimDecisionRecord> EventRecords(const AnimationPreviewSession& session)
    {
        std::vector<AnimDecisionRecord> records;
        for (const AnimationPreviewTickRecord& tick : session.History())
            for (const AnimDecisionRecord& record : tick.Decisions)
                if (record.Cause == AnimDecisionCause::EventCrossed)
                    records.push_back(record);
        return records;
    }

    std::vector<AnimationPreviewInvocation> Invocations(const AnimationPreviewSession& session)
    {
        std::vector<AnimationPreviewInvocation> invocations;
        for (const AnimationPreviewTickRecord& tick : session.History())
            invocations.insert(invocations.end(), tick.Invocations.begin(), tick.Invocations.end());
        return invocations;
    }
}

// Without a recorder a declared verb has nothing behind it in the preview and
// answers Unavailable; with one, the same crossing is accepted and the
// recorder shows what it was handed.
TEST(AnimationEventPreview, ACrossingIsAdmittedThroughTheNormalPath)
{
    EventPreviewFixture fx;
    AnimationPreviewSession session(fx.Data, &fx.Clips, &EventPreviewFixture::Vocabulary);
    ASSERT_TRUE(session.Open(EventPreviewFixture::Scenario())) << session.Problems().size();
    session.RunTo(20);

    std::vector<AnimDecisionRecord> records = EventRecords(session);
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].EventKey, 1u);
    EXPECT_EQ(records[0].Admission, VerbAdmission::Unavailable);
    EXPECT_TRUE(Invocations(session).empty());

    session.SetRecorder("test.footstep", true);
    EXPECT_EQ(session.Tick(), 20u) << "attaching a recorder replays to where the session was";
    session.RunTo(40);
    records = EventRecords(session);
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0].Admission, VerbAdmission::Accepted);
    EXPECT_EQ(records[1].EventKey, 2u);
    EXPECT_EQ(records[1].Admission, VerbAdmission::Unavailable);

    const std::vector<AnimationPreviewInvocation> invocations = Invocations(session);
    ASSERT_EQ(invocations.size(), 1u);
    EXPECT_EQ(invocations[0].Verb, "test.footstep");
    EXPECT_EQ(invocations[0].Binding, "anim.footstep");
    EXPECT_EQ(invocations[0].Producer, "subject");
    ASSERT_EQ(invocations[0].Arguments.size(), 2u);
    EXPECT_EQ(invocations[0].Arguments[0], std::make_pair(std::string("Surface"), std::string("Surface.Grass")));
    EXPECT_EQ(invocations[0].Arguments[1], std::make_pair(std::string("Volume"), std::string("0.5")));
}

// A client never produces a gameplay event; the same scenario as the
// authority does. The role is scenario state, so switching it replays.
TEST(AnimationEventPreview, TheRoleDecidesWhetherGameplayEventsAreProduced)
{
    EventPreviewFixture fx;
    AnimationScenario scenario = EventPreviewFixture::Scenario();
    scenario.Recorders = { "test.footstep", "test.hit" };
    scenario.Role = AnimationPreviewRole::Client;
    AnimationPreviewSession session(fx.Data, &fx.Clips, &EventPreviewFixture::Vocabulary);
    ASSERT_TRUE(session.Open(scenario));
    session.RunTo(40);
    std::vector<AnimationPreviewInvocation> invocations = Invocations(session);
    ASSERT_EQ(invocations.size(), 1u);
    EXPECT_EQ(invocations[0].Verb, "test.footstep");

    session.SetRole(AnimationPreviewRole::Authority);
    EXPECT_EQ(session.Tick(), 40u);
    invocations = Invocations(session);
    ASSERT_EQ(invocations.size(), 2u);
    EXPECT_EQ(invocations[1].Verb, "test.hit");
}

TEST(AnimationEventPreview, RoleAndRecordersAreSavedWithTheScenario)
{
    AnimationScenario scenario = EventPreviewFixture::Scenario();
    scenario.Role = AnimationPreviewRole::Client;
    scenario.Recorders = { "test.footstep" };
    std::vector<AnimDiagnostic> problems;
    const std::optional<AnimationScenario> reread =
        ReadAnimationScenario(WriteAnimationScenario(scenario), "walk.sanimscenario", problems);
    ASSERT_TRUE(reread.has_value());
    EXPECT_TRUE(problems.empty());
    EXPECT_EQ(reread->Role, AnimationPreviewRole::Client);
    EXPECT_EQ(reread->Recorders, scenario.Recorders);
}

// A saved scenario replays to the same crossings, admissions and recorded
// values, tick for tick.
TEST(AnimationEventPreview, AReplayProducesTheSameEvents)
{
    EventPreviewFixture fx;
    AnimationScenario scenario = EventPreviewFixture::Scenario();
    scenario.Recorders = { "test.footstep", "test.hit" };
    AnimationPreviewSession live(fx.Data, &fx.Clips, &EventPreviewFixture::Vocabulary);
    ASSERT_TRUE(live.Open(scenario));
    live.RunTo(130);

    AnimationPreviewSession replay(fx.Data, &fx.Clips, &EventPreviewFixture::Vocabulary);
    ASSERT_TRUE(replay.Open(live.Scenario()));
    replay.RunTo(130);
    ASSERT_EQ(live.History().size(), replay.History().size());
    for (std::size_t i = 0; i < live.History().size(); ++i)
        EXPECT_TRUE(SameAnimationPreviewTick(live.History()[i], replay.History()[i])) << "tick " << i;
    EXPECT_EQ(Invocations(live).size(), 4u);
}

// Scrubbing auditions content in its own session; it never crosses a mark in
// the simulation or reaches a verb.
TEST(AnimationEventPreview, AuditioningNeverDispatches)
{
    EventPreviewFixture fx;
    AnimationScenario scenario = EventPreviewFixture::Scenario();
    scenario.Recorders = { "test.footstep" };
    AnimationPreviewSession session(fx.Data, &fx.Clips, &EventPreviewFixture::Vocabulary);
    ASSERT_TRUE(session.Open(scenario));
    session.RunTo(5);
    const std::size_t history = session.History().size();

    AnimationClipPreviewSession audition;
    SkeletonData skeleton;
    SkeletonJoint root;
    root.InverseBind = Mat4::Identity();
    skeleton.Joints.push_back(root);
    AnimationClipData clip = *fx.Clips.Get(fx.Clips.Find(kClip));
    std::string error;
    ASSERT_TRUE(audition.SetContent(std::string(clip.SkeletonPath), skeleton, clip, error)) << error;
    for (const double at : { 0.0, 0.2, 0.26, 0.5, 0.9 })
    {
        audition.InspectNormalized(at);
        (void)audition.Palette();
    }
    audition.Play();
    audition.Advance(2.0);

    EXPECT_EQ(session.History().size(), history);
    EXPECT_EQ(session.Tick(), 5u);
    EXPECT_TRUE(Invocations(session).empty());
}

// Bindings are named by key and looked up at each crossing: removing one
// leaves its events unresolved, and restoring it resolves them again, with no
// pointer or verb id held across the change.
TEST(AnimationEventPreview, ABindingRemovedAndRestoredIsFollowedByKey)
{
    EventPreviewFixture fx;
    AnimationScenario scenario = EventPreviewFixture::Scenario();
    scenario.Recorders = { "test.footstep" };
    AnimationPreviewSession session(fx.Data, &fx.Clips, &EventPreviewFixture::Vocabulary);
    ASSERT_TRUE(session.Open(scenario));
    session.RunTo(20);
    ASSERT_EQ(EventRecords(session).back().Admission, VerbAdmission::Accepted);

    // Reordered, and without the footstep.
    fx.Reload(kBindingsPath, kVerbBindingsTypeName,
              R"({ "bindings": [ { "key": "melee.hit", "verb": "test.hit", "arguments": {} } ] })");
    session.Rebind();
    session.RunTo(80);
    ASSERT_EQ(EventRecords(session).back().EventKey, 1u);
    EXPECT_EQ(EventRecords(session).back().Admission, VerbAdmission::UnresolvedBinding);

    fx.Reload(kBindingsPath, kVerbBindingsTypeName, kBindings);
    session.Rebind();
    session.RunTo(140);
    EXPECT_EQ(EventRecords(session).back().EventKey, 1u);
    EXPECT_EQ(EventRecords(session).back().Admission, VerbAdmission::Accepted);
}
