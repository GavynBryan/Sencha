// Blend A/B without a GUI: record the simulation's poses as take A, edit a
// blend policy through its document, replay the same scenario from tick 0,
// and read where B differs from A -- only while the edited blend runs.

#include "authoring/AnimationBlendComparison.h"
#include "authoring/AnimationPreviewWorkspace.h"

#include <anim/AnimBehaviorSet.h>
#include <anim/AnimationClipCache.h>
#include <anim/SkeletonCache.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    constexpr const char* kSkeleton = "asset://anim/walker.sskel";

    struct Project
    {
        std::filesystem::path Root = std::filesystem::temp_directory_path() / "sencha_blend_editing";
        LoggingProvider Logging;
        ComponentSerializerRegistry Serializers;
        std::unique_ptr<RuntimeAssets> Assets;
        std::vector<AssetLease> Leases;

        Project()
        {
            std::filesystem::remove_all(Root);
            std::filesystem::create_directories(Root / "anim");
            Write("walker.facts.sdata", R"({ "type": "animation.fact_schema", "version": 1, "data": {
                "slots": [ { "name": "Speed", "kind": "float" } ] } })");
            Write("walker.behaviors.sdata", R"({ "type": "animation.behavior_set", "version": 1, "data": {
                "behaviors": [ { "tag": "Anim.Idle", "kind": "cyclic" },
                               { "tag": "Anim.Walk", "kind": "cyclic",
                                 "blend": { "in": "inertialize", "in_ms": 100 } } ] } })");
            Write("walker.selector.sdata", R"({ "type": "animation.selector", "version": 1, "data": { "rules": [
                { "name": "walk", "priority": 10, "enter": [ { "fact": "Speed", "compare": "gt", "value": 0.1 } ],
                  "behavior": "Anim.Walk" },
                { "name": "idle", "priority": 0, "enter": [], "behavior": "Anim.Idle" } ] } })");
            Write("walker.slots.sdata", R"({ "type": "animation.slot_map", "version": 1, "data": { "rows": [
                { "id": "idle", "behavior": "Anim.Idle", "clip": "asset://anim/idle.sanim" },
                { "id": "walk", "behavior": "Anim.Walk", "clip": "asset://anim/walk.sanim" } ] } })");
            Write("walker.rig.sdata", R"({ "type": "animation.rig", "version": 1, "data": {
                "skeleton": "asset://anim/walker.sskel", "facts": "asset://anim/walker.facts.sdata",
                "behaviors": [ "asset://anim/walker.behaviors.sdata" ], "slot_maps": [ "asset://anim/walker.slots.sdata" ],
                "layers": [ { "name": "anim.layer.base", "selector": "asset://anim/walker.selector.sdata",
                              "idle": "Anim.Idle" } ] } })");
            Write("walker.rig.sanimscenario", R"({ "type": "animation.preview_scenario", "version": 1,
                "name": "start walking", "rig": "asset://anim/walker.rig.sdata", "participants": [ "player" ],
                "declared_tags": [ "Anim.Idle", "Anim.Walk" ], "inputs": { "Speed": 0.0 },
                "actions": [ { "tick": 30, "set": "Speed", "value": 1.0 } ] })");

            Assets = std::make_unique<RuntimeAssets>(Logging, Serializers);
            SkeletonData skeleton;
            SkeletonJoint root;
            root.Name = "root";
            skeleton.Joints.push_back(root);
            EXPECT_TRUE(Assets->Registry.RegisterOrVerify(AssetRecord{
                .Type = AssetType::Skeleton, .SourceKind = AssetSourceKind::Procedural, .Path = kSkeleton }));
            (void)Assets->Skeletons.Register(kSkeleton, std::move(skeleton));
            Leases.push_back(Assets->Assets.TryAcquireLease(kSkeleton, AssetType::Skeleton));
            for (const auto& [path, x] : { std::pair{ "asset://anim/idle.sanim", 0.0f },
                                           std::pair{ "asset://anim/walk.sanim", 4.0f } })
            {
                EXPECT_TRUE(Assets->Registry.RegisterOrVerify(AssetRecord{
                    .Type = AssetType::AnimationClip, .SourceKind = AssetSourceKind::Procedural, .Path = path }));
                AnimationClipData clip;
                clip.DurationSeconds = 1.0f;
                clip.SkeletonPath = kSkeleton;
                AnimationJointTrack track;
                track.Path = AnimationChannelPath::Translation;
                track.TimesSeconds = { 0.0f };
                track.Values = { x, 0.0f, 0.0f };
                clip.Tracks.push_back(std::move(track));
                (void)Assets->AnimationClips.Register(path, std::move(clip), Assets->Skeletons.AcquireOwned(kSkeleton));
                Leases.push_back(Assets->Assets.TryAcquireLease(path, AssetType::AnimationClip));
            }
            (void)ScanAssetsDirectory(Root.generic_string(), Assets->Registry, Assets->Assets.Kinds());
        }

        ~Project()
        {
            Leases.clear();
            std::filesystem::remove_all(Root);
        }

        void Write(const std::string& name, std::string_view text) { std::ofstream(Root / "anim" / name) << text; }
    };
}

TEST(AnimationBlendEditing, ReplayingAnEditedBlendDiffersOnlyWhileItRuns)
{
    Project project;
    {
        AnimationPreviewWorkspace workspace(*project.Assets);
        ASSERT_TRUE(workspace.OpenRig("asset://anim/walker.rig.sdata")) << workspace.Rig.Error;
        ASSERT_TRUE(workspace.Rig.Simulation.Rig()->Valid);
        workspace.Rig.Simulation.RunTo(90);
        ASSERT_TRUE(workspace.Takes.RecordA(workspace.Rig.Simulation));
        ASSERT_EQ(workspace.Takes.A()->Ticks.back(), 90u);

        // Lengthen the walk's inertialization through the behavior set's
        // document, as the Behavior panel or Data Editor would.
        ASSERT_TRUE((workspace.Documents.OpenOrFocus("asset://anim/walker.behaviors.sdata", workspace.DocumentError) != nullptr));
        DataDocument* behaviors = workspace.Documents.ActiveOf(kAnimBehaviorSetType);
        ASSERT_NE(behaviors, nullptr);
        JsonValue root = behaviors->CopyRoot();
        (*root.Find("data")->Find("behaviors")->AsArray()[1].Find("blend")->Find("in_ms")) = JsonValue(300.0);
        behaviors->BeginEdit();
        behaviors->PreviewRoot(std::move(root));
        workspace.Documents.CommitEdit(*behaviors);

        ASSERT_TRUE(workspace.Takes.ReplayAgainstA(workspace.Rig.Simulation)) << workspace.Takes.Comparison().Refusal;
        const std::vector<AnimationPoseResidual>& residuals = workspace.Takes.Comparison().Residuals;
        ASSERT_FALSE(residuals.empty());
        const auto at = [&](AnimTick tick) {
            const auto it = std::ranges::find_if(residuals, [&](const AnimationPoseResidual& r) { return r.Tick == tick; });
            return it != residuals.end() ? it->Position : -1.0f;
        };
        // Identical until the change, identical once both have settled, and
        // apart while B's longer blend is still running past A's.
        EXPECT_FLOAT_EQ(at(29), 0.0f);
        EXPECT_FLOAT_EQ(at(30), 0.0f) << "both show what was shown on the change tick";
        EXPECT_GT(at(40), 0.1f);
        EXPECT_FLOAT_EQ(at(60), 0.0f);
        EXPECT_FLOAT_EQ(at(90), 0.0f);

        // A take under another scenario does not compare.
        AnimationScenarioValue faster;
        faster.Number = 2.0;
        workspace.Rig.Simulation.SetFact("Speed", faster);
        workspace.Rig.Simulation.Step();
        EXPECT_FALSE(workspace.Takes.ReplayAgainstA(workspace.Rig.Simulation));
        EXPECT_NE(workspace.Takes.Comparison().Refusal.find("different scenarios"), std::string::npos);
        workspace.Rig.Simulation.Close();
        workspace.Sources.DiscardAll();
    }
}
