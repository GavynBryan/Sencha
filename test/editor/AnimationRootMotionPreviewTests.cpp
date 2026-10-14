// A scenario that puts the character on a floor runs the production movement
// pipeline after content: a root-motion clip carries it, a placed wall stops it,
// and each tick records where the clip carried it against where it got.

#include "properties/PropertyTree.h"
#include "EditorDocumentsFixture.h"
#include "authoring/AnimationPreviewSession.h"
#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationRootMotionProperties.h"
#include "authoring/AnimationScenario.h"
#include "authoring/AnimationTimelineLanes.h"

#include <anim/AnimationClipCache.h>
#include <anim/SkeletonCache.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    constexpr const char* kSkeleton = "asset://anim/runner.sskel";

    struct Project
    {
        std::filesystem::path Root = std::filesystem::temp_directory_path() / "sencha_root_motion_preview";
        LoggingProvider Logging;
        ComponentSerializerRegistry Serializers;
        std::unique_ptr<RuntimeAssets> Assets;
        std::vector<AssetLease> Leases;

        Project()
        {
            std::filesystem::remove_all(Root);
            std::filesystem::create_directories(Root / "anim");
            Write("runner.requests.sdata", R"({ "type": "animation.request_schema", "version": 1, "data": {
                "intents": [ { "intent": "anim.intent.dash", "params": [] } ] } })");
            Write("runner.behaviors.sdata", R"({ "type": "animation.behavior_set", "version": 1, "data": {
                "behaviors": [ { "tag": "Anim.Idle", "kind": "cyclic" },
                               { "tag": "anim.intent.dash", "kind": "one_shot", "root_motion": true } ] } })");
            Write("runner.slots.sdata", R"({ "type": "animation.slot_map", "version": 1, "data": { "rows": [
                { "id": "idle", "behavior": "Anim.Idle", "clip": "asset://anim/idle.sanim" },
                { "id": "dash", "behavior": "anim.intent.dash", "clip": "asset://anim/dash.sanim" } ] } })");
            Write("runner.rig.sdata", R"({ "type": "animation.rig", "version": 1, "data": {
                "skeleton": "asset://anim/runner.sskel", "requests": "asset://anim/runner.requests.sdata",
                "behaviors": [ "asset://anim/runner.behaviors.sdata" ], "slot_maps": [ "asset://anim/runner.slots.sdata" ],
                "layers": [ { "name": "anim.layer.base", "idle": "Anim.Idle" } ] } })");

            Assets = std::make_unique<RuntimeAssets>(Logging, Serializers);
            SkeletonData skeleton;
            SkeletonJoint root;
            root.Name = "root";
            skeleton.Joints.push_back(root);
            EXPECT_TRUE(Assets->Registry.RegisterOrVerify(AssetRecord{
                .Type = AssetType::Skeleton, .SourceKind = AssetSourceKind::Procedural, .Path = kSkeleton }));
            (void)Assets->Skeletons.Register(kSkeleton, std::move(skeleton));
            Leases.push_back(Assets->Assets.TryAcquireLease(kSkeleton, AssetType::Skeleton));
            for (const auto& [path, carries] : { std::pair{ "asset://anim/idle.sanim", false },
                                                 std::pair{ "asset://anim/dash.sanim", true } })
            {
                EXPECT_TRUE(Assets->Registry.RegisterOrVerify(AssetRecord{
                    .Type = AssetType::AnimationClip, .SourceKind = AssetSourceKind::Procedural, .Path = path }));
                AnimationClipData clip;
                clip.DurationSeconds = 1.0f;
                clip.SkeletonPath = kSkeleton;
                AnimationJointTrack stand;
                stand.Path = AnimationChannelPath::Translation;
                stand.TimesSeconds = { 0.0f };
                stand.Values = { 0.0f, 0.0f, 0.0f };
                clip.Tracks.push_back(stand);
                if (carries)
                    clip.Root = AnimationRootCurve{ .TimesSeconds = { 0.0f, 1.0f },
                                                    .Values = { 0.0f, 0.0f, 0.0f, 0.0f, -3.0f, 0.0f } };
                (void)Assets->AnimationClips.Register(path, std::move(clip), Assets->Skeletons.AcquireOwned(kSkeleton));
                Leases.push_back(Assets->Assets.TryAcquireLease(path, AssetType::AnimationClip));
            }
            (void)ScanAssetsDirectory(Root.generic_string(), Assets->Registry, Assets->Assets.Kinds());
            Leases.push_back(Assets->Assets.LoadLease("asset://anim/runner.rig.sdata", AssetType::Data));
            EXPECT_TRUE(Leases.back());
        }

        ~Project()
        {
            Leases.clear();
            std::filesystem::remove_all(Root);
        }

        void Write(const std::string& name, std::string_view text) { std::ofstream(Root / "anim" / name) << text; }

        // Dash on tick 10, moving, with these walls.
        static AnimationScenario Dash(std::vector<AnimationScenarioWall> walls)
        {
            AnimationScenario scenario;
            scenario.Name = "dash";
            scenario.RigPath = "asset://anim/runner.rig.sdata";
            scenario.Participants = { "player" };
            scenario.DeclaredTags = { "Anim.Idle", "anim.intent.dash" };
            scenario.Movement = AnimationScenarioMovement{ .Walls = std::move(walls) };
            AnimationScenarioAction dash;
            dash.Tick = 10;
            dash.Kind = AnimationScenarioActionKind::IssueRequest;
            dash.Intent = "anim.intent.dash";
            dash.Participant = "player";
            scenario.Append(dash);
            return scenario;
        }
    };

}

TEST(AnimationRootMotionPreview, AClipCarriesTheCharacterAcrossTheFloor)
{
    Project project;
    AnimationPreviewSession session(project.Assets->DataAssets, &project.Assets->AnimationClips, {},
                                    &project.Assets->Skeletons);
    ASSERT_TRUE(session.Open(Project::Dash({})));
    ASSERT_TRUE(session.Rig()->Valid);
    session.RunTo(90);
    ASSERT_NE(session.SubjectTransform(), nullptr);
    EXPECT_NEAR(session.SubjectTransform()->Position.Z, -3.0f, 0.05f);

    int carried = 0;
    for (const AnimationPreviewTickRecord& record : session.History())
    {
        ASSERT_TRUE(record.Movement.has_value()) << "tick " << record.Tick;
        carried += record.Movement->Carried ? 1 : 0;
        EXPECT_FALSE(record.Movement->Blocked) << "tick " << record.Tick;
    }
    EXPECT_GE(carried, 59) << "carried for the clip's second";
}

TEST(AnimationRootMotionPreview, AWallTheScenarioPlacesStopsTheCharacter)
{
    Project project;
    AnimationPreviewSession session(project.Assets->DataAssets, &project.Assets->AnimationClips, {},
                                    &project.Assets->Skeletons);
    // Its face 1.5 m ahead of where the character starts.
    ASSERT_TRUE(session.Open(
        Project::Dash({ AnimationScenarioWall{ .Center = Vec3d(0.0f, 1.0f, -1.75f),
                                               .HalfExtents = Vec3d(2.0f, 1.0f, 0.25f) } })));
    session.RunTo(90);
    EXPECT_GT(session.SubjectTransform()->Position.Z, -1.5f);

    AnimTick firstBlocked = 0;
    for (const AnimationPreviewTickRecord& record : session.History())
        if (record.Movement && record.Movement->Blocked && firstBlocked == 0)
            firstBlocked = record.Tick;
    EXPECT_GT(firstBlocked, 10u) << "the wall is reached a while into the dash";
    EXPECT_LT(firstBlocked, 70u);
}

// Movement is scenario state: taken away, the character poses in place; it
// survives a save.
TEST(AnimationRootMotionPreview, MovementIsScenarioState)
{
    Project project;
    AnimationPreviewSession session(project.Assets->DataAssets, &project.Assets->AnimationClips, {},
                                    &project.Assets->Skeletons);
    const AnimationScenario scenario =
        Project::Dash({ AnimationScenarioWall{ .Center = Vec3d(1.0f, 2.0f, 3.0f), .HalfExtents = Vec3d(0.5f, 0.25f, 4.0f) } });
    std::vector<AnimDiagnostic> diagnostics;
    const std::optional<AnimationScenario> reread =
        ReadAnimationScenario(WriteAnimationScenario(scenario), "dash.sanimscenario", diagnostics);
    ASSERT_TRUE(reread.has_value());
    EXPECT_TRUE(diagnostics.empty());
    EXPECT_EQ(reread->Movement, scenario.Movement);

    ASSERT_TRUE(session.Open(scenario));
    session.RunTo(20);
    session.SetMovement(std::nullopt);
    EXPECT_EQ(session.Tick(), 20u) << "replayed to where it was";
    EXPECT_EQ(session.SubjectTransform(), nullptr);
    EXPECT_FALSE(session.History().back().Movement.has_value());
}

// The deck plots how far the clip carried the character on each tick against
// how far it got: against a wall the two part.
TEST(AnimationRootMotionPreview, TheTravelLanesPlotCarriedAgainstAchieved)
{
    Project project;
    AnimationPreviewSession session(project.Assets->DataAssets, &project.Assets->AnimationClips, {},
                                    &project.Assets->Skeletons);
    ASSERT_TRUE(session.Open(
        Project::Dash({ AnimationScenarioWall{ .Center = Vec3d(0.0f, 1.0f, -1.75f),
                                               .HalfExtents = Vec3d(2.0f, 1.0f, 0.25f) } })));
    session.RunTo(90);
    const std::vector<TimelineLane> lanes = AnimationTravelLanes(session);
    ASSERT_EQ(lanes.size(), 2u);
    EXPECT_EQ(lanes[0].Label, "Carried");
    EXPECT_EQ(lanes[1].Label, "Achieved");
    ASSERT_EQ(lanes[0].Samples.size(), lanes[1].Samples.size());
    ASSERT_FALSE(lanes[0].Samples.empty());
    double carried = 0.0;
    double achieved = 0.0;
    for (std::size_t i = 0; i < lanes[0].Samples.size(); ++i)
    {
        carried += lanes[0].Samples[i].Value;
        achieved += lanes[1].Samples[i].Value;
    }
    EXPECT_GT(carried, achieved + 1.0) << "the wall stopped it short";

    session.SetMovement(std::nullopt);
    EXPECT_TRUE(AnimationTravelLanes(session).empty()) << "posing in place, nothing travels";
}

// The card moves the character or not, and places the walls, as scenario state.
TEST(AnimationRootMotionPreview, TheCardPlacesTheScenariosWalls)
{
    Project project;
    EditorDocuments documents(*project.Assets);
    AnimationPreviewWorkspace workspace(*project.Assets, documents.Sources, documents.Store);
    AnimationPreviewSession& session = workspace.Rig.Simulation;
    ASSERT_TRUE(session.Open(Project::Dash({})));
    session.RunTo(30);
    AnimationRootMotionProperties card(workspace);

    std::vector<PropertyNode> nodes = card.Nodes();
    ASSERT_NE(FindProperty(nodes, "moves"), nullptr);
    EXPECT_EQ(FindProperty(nodes, "moves")->Value, "true");
    ASSERT_NE(FindProperty(nodes, "clip.root"), nullptr);
    EXPECT_EQ(FindProperty(nodes, "clip.root")->Value, "Carrying the character");
    EXPECT_EQ(FindProperty(nodes, "walls.0"), nullptr);

    EXPECT_EQ(card.Act("walls", "add", ""), PropertyEditResult::Applied);
    ASSERT_EQ(session.Scenario().Movement->Walls.size(), 1u);
    ASSERT_TRUE(card.BeginEdit("walls.0.centre"));
    EXPECT_EQ(card.CommitEdit("walls.0.centre", "0, 1, -3"), PropertyEditResult::Applied);
    EXPECT_EQ(session.Scenario().Movement->Walls[0].Center, Vec3d(0.0f, 1.0f, -3.0f));
    EXPECT_EQ(card.CommitEdit("walls.0.half", "0, 1, 1"), PropertyEditResult::Applied);
    EXPECT_FLOAT_EQ(session.Scenario().Movement->Walls[0].HalfExtents.X, 0.01f) << "a wall keeps some thickness";
    EXPECT_EQ(card.CommitEdit("walls.0.half", "wide"), PropertyEditResult::Refused);
    EXPECT_EQ(card.Act("walls.0", "remove", ""), PropertyEditResult::Applied);
    EXPECT_TRUE(session.Scenario().Movement->Walls.empty());

    EXPECT_EQ(card.CommitEdit("moves", "false"), PropertyEditResult::Applied);
    EXPECT_FALSE(session.Scenario().Movement.has_value());
    nodes = card.Nodes();
    EXPECT_EQ(FindProperty(nodes, "walls"), nullptr);
    EXPECT_EQ(FindProperty(nodes, "travel"), nullptr);
    session.Close();
}
