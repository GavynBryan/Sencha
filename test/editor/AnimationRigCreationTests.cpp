// A new rig from a name and its clips: four documents and a scenario written
// into the project, registered, and opened ready to play -- the first clip
// idling, the others played by holding a request of their name.

#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationRigRecipe.h"

#include <anim/AnimationClipCache.h>
#include <anim/SkeletonCache.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <filesystem>

namespace
{
    struct Project
    {
        std::filesystem::path Root = std::filesystem::temp_directory_path() / "sencha_rig_creation";
        LoggingProvider Logging;
        ComponentSerializerRegistry Serializers;
        std::unique_ptr<RuntimeAssets> Assets;
        std::vector<AssetLease> Leases;

        Project()
        {
            std::filesystem::remove_all(Root);
            std::filesystem::create_directories(Root);
            Assets = std::make_unique<RuntimeAssets>(Logging, Serializers);
            for (const char* skeleton : { "asset://meshes/man.blend#skel:Man", "asset://meshes/dog.blend#skel:Dog" })
            {
                SkeletonData data;
                SkeletonJoint root;
                root.Name = "root";
                data.Joints.push_back(root);
                EXPECT_TRUE(Assets->Registry.RegisterOrVerify(AssetRecord{
                    .Type = AssetType::Skeleton, .SourceKind = AssetSourceKind::Procedural, .Path = skeleton }));
                (void)Assets->Skeletons.Register(skeleton, std::move(data));
                Leases.push_back(Assets->Assets.TryAcquireLease(skeleton, AssetType::Skeleton));
            }
            Clip("asset://meshes/man.blend#anim:Idle", "asset://meshes/man.blend#skel:Man");
            Clip("asset://meshes/man.blend#anim:Walk", "asset://meshes/man.blend#skel:Man");
            Clip("asset://meshes/man.blend#anim:Left_claw", "asset://meshes/man.blend#skel:Man");
            // The engine's fact schema, which the selector tiers read.
            (void)ScanAssetsDirectory((std::filesystem::path(SENCHA_REPO_ROOT) / "engine/assets").generic_string(),
                                      Assets->Registry, Assets->Assets.Kinds());
            Clip("asset://meshes/dog.blend#anim:Sit", "asset://meshes/dog.blend#skel:Dog");
        }

        ~Project()
        {
            Leases.clear();
            std::filesystem::remove_all(Root);
        }

        void Clip(const char* path, const char* skeleton)
        {
            EXPECT_TRUE(Assets->Registry.RegisterOrVerify(
                AssetRecord{ .Type = AssetType::AnimationClip, .SourceKind = AssetSourceKind::Procedural, .Path = path }));
            AnimationClipData clip;
            clip.DurationSeconds = 1.0f;
            clip.SkeletonPath = skeleton;
            (void)Assets->AnimationClips.Register(path, std::move(clip), Assets->Skeletons.AcquireOwned(skeleton));
            Leases.push_back(Assets->Assets.TryAcquireLease(path, AssetType::AnimationClip));
        }
    };
}

TEST(AnimationRigRecipe, AClipNamesTheBehaviorItPlaysAs)
{
    EXPECT_EQ(AnimationRigBehaviorFor("asset://meshes/man.blend#anim:Left_claw"), "Anim.Left_claw");
    EXPECT_EQ(AnimationRigBehaviorFor("asset://anim/walk.sanim"), "Anim.Walk");
    EXPECT_EQ(AnimationRigBehaviorFor("asset://anim/2 step.sanim"), "Anim.Clip_2_step");
    EXPECT_EQ(AnimationRigBehaviorFor("asset://x.blend#anim:run-fast"), "Anim.Run_fast");
}

TEST(AnimationRigCreation, ANewRigOpensReadyToPlay)
{
    Project project;
    AnimationPreviewWorkspace workspace(*project.Assets, {}, project.Root);
    std::string error;
    ASSERT_TRUE(workspace.CreateRig({ .Name = "praying_man", .Clips = { "asset://meshes/man.blend#anim:Idle",
                                                       "asset://meshes/man.blend#anim:Left_claw" }, .Preset = AnimationRigPreset::Prop, .UpperBodyJoint = {} },
                                    error))
        << error;
    for (const char* file : { "praying_man.behaviors.sdata", "praying_man.slots.sdata", "praying_man.requests.sdata",
                              "praying_man.rig.sdata", "praying_man.rig.sanimscenario" })
        EXPECT_TRUE(std::filesystem::exists(project.Root / "animation/praying_man" / file)) << file;

    ASSERT_EQ(workspace.RigPath, "asset://animation/praying_man/praying_man.rig.sdata");
    const AnimBoundRig* rig = workspace.Simulation.Rig();
    ASSERT_NE(rig, nullptr);
    ASSERT_TRUE(rig->Valid) << (rig->Diagnostics.empty() ? "" : FormatAnimDiagnostic(rig->Diagnostics.front()));
    EXPECT_EQ(rig->SkeletonPath, "asset://meshes/man.blend#skel:Man");
    const auto playing = [&] {
        const AnimContentState* content = workspace.Simulation.Content();
        return rig->Contents[content->Layers[0].Content].Path;
    };
    workspace.Simulation.Step();
    EXPECT_EQ(playing(), "asset://meshes/man.blend#anim:Idle");

    AnimationScenarioAction claw;
    claw.Kind = AnimationScenarioActionKind::IssueRequest;
    claw.Participant = "player";
    claw.Intent = "Anim.Left_claw";
    workspace.Simulation.IssueRequest(claw);
    workspace.Simulation.Step();
    rig = workspace.Simulation.Rig();
    EXPECT_EQ(playing(), "asset://meshes/man.blend#anim:Left_claw");
    workspace.Simulation.Close();

    // Created once: the same name again is refused rather than overwritten.
    EXPECT_FALSE(workspace.CreateRig({ .Name = "praying_man", .Clips = { "asset://meshes/man.blend#anim:Idle" }, .Preset = AnimationRigPreset::Prop, .UpperBodyJoint = {} }, error));
    EXPECT_NE(error.find("already exists"), std::string::npos) << error;
}

TEST(AnimationRigCreation, ARecipeNeedsANameClipsAndOneSkeleton)
{
    Project project;
    AnimationPreviewWorkspace workspace(*project.Assets, {}, project.Root);
    std::string error;
    EXPECT_FALSE(workspace.CreateRig({ .Name = "bad name", .Clips = { "asset://meshes/man.blend#anim:Idle" }, .Preset = AnimationRigPreset::Prop, .UpperBodyJoint = {} }, error));
    EXPECT_NE(error.find("letters, digits and underscores"), std::string::npos);
    EXPECT_FALSE(workspace.CreateRig({ .Name = "empty", .Clips = {}, .Preset = AnimationRigPreset::Prop, .UpperBodyJoint = {} }, error));
    EXPECT_FALSE(workspace.CreateRig({ .Name = "mixed", .Clips = { "asset://meshes/man.blend#anim:Idle",
                                                  "asset://meshes/dog.blend#anim:Sit" }, .Preset = AnimationRigPreset::Prop, .UpperBodyJoint = {} },
                                     error));
    EXPECT_NE(error.find("one rig poses one skeleton"), std::string::npos) << error;
    EXPECT_FALSE(std::filesystem::exists(project.Root / "animation")) << "a refused recipe writes nothing";

    AnimationPreviewWorkspace readOnly(*project.Assets);
    EXPECT_FALSE(readOnly.CreateRig({ .Name = "man", .Clips = { "asset://meshes/man.blend#anim:Idle" }, .Preset = AnimationRigPreset::Prop, .UpperBodyJoint = {} }, error));
    EXPECT_NE(error.find("content root"), std::string::npos);
}

namespace
{
    std::string Playing(AnimationPreviewWorkspace& workspace, std::size_t layer)
    {
        const AnimBoundRig* rig = workspace.Simulation.Rig();
        const AnimContentState* content = workspace.Simulation.Content();
        const std::uint16_t playing = content->Layers[layer].Content;
        return playing < rig->Contents.size() ? rig->Contents[playing].Path : std::string("(none)");
    }

    void Request(AnimationPreviewWorkspace& workspace, const char* intent,
                 AnimRequestLifetime lifetime = AnimRequestLifetime::Held)
    {
        AnimationScenarioAction action;
        action.Kind = AnimationScenarioActionKind::IssueRequest;
        action.Participant = "player";
        action.Intent = intent;
        action.Lifetime = lifetime;
        workspace.Simulation.IssueRequest(action);
    }

    void SetSpeed(AnimationPreviewWorkspace& workspace, double speed)
    {
        AnimationScenarioValue value;
        value.Type = AnimationScenarioValue::Kind::Number;
        value.Number = speed;
        workspace.Simulation.SetFact("Speed", value);
    }
}

// A Simple rig: rules over the engine's facts idle it, walk it when it moves,
// and play an action through once when asked.
TEST(AnimationRigCreation, ASimpleRigIdlesWalksAndActs)
{
    Project project;
    AnimationPreviewWorkspace workspace(*project.Assets, {}, project.Root);
    std::string error;
    ASSERT_TRUE(workspace.CreateRig({ .Name = "brute",
                                      .Clips = { "asset://meshes/man.blend#anim:Idle", "asset://meshes/man.blend#anim:Walk",
                                                 "asset://meshes/man.blend#anim:Left_claw" },
                                      .Preset = AnimationRigPreset::Simple, .UpperBodyJoint = {} },
                                    error))
        << error;
    EXPECT_TRUE(std::filesystem::exists(project.Root / "animation/brute/brute.selector.sdata"));
    const AnimBoundRig* rig = workspace.Simulation.Rig();
    ASSERT_TRUE(rig != nullptr && rig->Valid)
        << (rig == nullptr || rig->Diagnostics.empty() ? "" : FormatAnimDiagnostic(rig->Diagnostics.front()));
    ASSERT_EQ(rig->Layers.size(), 1u);

    workspace.Simulation.Step();
    EXPECT_EQ(Playing(workspace, 0), "asset://meshes/man.blend#anim:Idle");
    SetSpeed(workspace, 2.0);
    workspace.Simulation.Step();
    workspace.Simulation.Step();
    EXPECT_EQ(Playing(workspace, 0), "asset://meshes/man.blend#anim:Walk");
    // An action is asked for once, as a game fires one.
    Request(workspace, "Anim.Left_claw", AnimRequestLifetime::Impulse);
    workspace.Simulation.Step();
    EXPECT_EQ(Playing(workspace, 0), "asset://meshes/man.blend#anim:Left_claw");
    // Played through once, then back to walking.
    workspace.Simulation.RunTo(workspace.Simulation.Tick() + 70);
    EXPECT_EQ(Playing(workspace, 0), "asset://meshes/man.blend#anim:Walk");
    workspace.Simulation.Close();
}

// A Character rig acts on its upper body over its walk, and its upper layer
// shows only while it acts.
TEST(AnimationRigCreation, ACharacterRigActsOverItsLocomotion)
{
    Project project;
    AnimationPreviewWorkspace workspace(*project.Assets, {}, project.Root);
    std::string error;
    ASSERT_TRUE(workspace.CreateRig({ .Name = "hero",
                                      .Clips = { "asset://meshes/man.blend#anim:Idle", "asset://meshes/man.blend#anim:Walk",
                                                 "asset://meshes/man.blend#anim:Left_claw" },
                                      .Preset = AnimationRigPreset::Character,
                                      .UpperBodyJoint = "root" },
                                    error))
        << error;
    const AnimBoundRig* rig = workspace.Simulation.Rig();
    ASSERT_TRUE(rig != nullptr && rig->Valid)
        << (rig == nullptr || rig->Diagnostics.empty() ? "" : FormatAnimDiagnostic(rig->Diagnostics.front()));
    ASSERT_EQ(rig->Layers.size(), 2u);
    EXPECT_TRUE(rig->Layers[1].Masked());

    SetSpeed(workspace, 2.0);
    workspace.Simulation.Step();
    workspace.Simulation.Step();
    EXPECT_EQ(Playing(workspace, 0), "asset://meshes/man.blend#anim:Walk");
    EXPECT_FLOAT_EQ(workspace.Simulation.History().back().Layers[1].Weight, 0.0f) << "hidden while it does nothing";

    Request(workspace, "Anim.Left_claw");
    workspace.Simulation.Step();
    EXPECT_EQ(Playing(workspace, 0), "asset://meshes/man.blend#anim:Walk") << "the legs keep walking";
    EXPECT_EQ(Playing(workspace, 1), "asset://meshes/man.blend#anim:Left_claw");
    EXPECT_FLOAT_EQ(workspace.Simulation.History().back().Layers[1].Weight, 1.0f);
    workspace.Simulation.Close();
}

TEST(AnimationRigCreation, ACharacterNeedsItsActionsAndAnUpperBodyJoint)
{
    Project project;
    AnimationPreviewWorkspace workspace(*project.Assets, {}, project.Root);
    std::string error;
    const std::vector<std::string> clips = { "asset://meshes/man.blend#anim:Idle", "asset://meshes/man.blend#anim:Walk",
                                             "asset://meshes/man.blend#anim:Left_claw" };
    EXPECT_FALSE(workspace.CreateRig({ .Name = "a", .Clips = { clips[0], clips[1] }, .Preset = AnimationRigPreset::Character,
                                       .UpperBodyJoint = "root" },
                                     error));
    EXPECT_NE(error.find("at least one action"), std::string::npos) << error;
    EXPECT_FALSE(workspace.CreateRig({ .Name = "b", .Clips = clips, .Preset = AnimationRigPreset::Character, .UpperBodyJoint = {} }, error));
    EXPECT_NE(error.find("upper body"), std::string::npos) << error;
    EXPECT_FALSE(workspace.CreateRig({ .Name = "c", .Clips = clips, .Preset = AnimationRigPreset::Character,
                                       .UpperBodyJoint = "spine" },
                                     error));
    EXPECT_NE(error.find("no joint named 'spine'"), std::string::npos) << error;
}
