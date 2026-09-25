// A long authoring session holds steady: the same work done over and over
// leaves the same assets resident, the same preview World, bounded history,
// and no interaction open behind it.

#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationRigRecipe.h"

#include "AnimationAuthoringSteps.h"
#include "AnimationTestProject.h"

#include <assets/runtime/AssetSystem.h>
#include <ecs/World.h>

#include <gtest/gtest.h>

#include <format>
#include <string>

namespace
{
    constexpr const char* kSkeleton = "asset://meshes/man.blend#skel:Man";
    constexpr const char* kCharacterRig = "asset://animation/character_rig/character_rig.rig.sdata";
    constexpr const char* kSimpleRig = "asset://animation/simple_rig/simple_rig.rig.sdata";

    // What the session holds, compared whole between rounds.
    struct Footprint
    {
        std::size_t ResidentAssets = 0;
        std::size_t Documents = 0;
        std::size_t PreviewEntities = 0;
        std::size_t History = 0;
        std::size_t ContentTags = 0;
        friend bool operator==(const Footprint&, const Footprint&) = default;
    };

    std::string Describe(const Footprint& f)
    {
        return std::format("{} resident, {} documents, {} preview entities, {} history, {} tags", f.ResidentAssets,
                           f.Documents, f.PreviewEntities, f.History, f.ContentTags);
    }

    Footprint Measure(AnimationTestProject& project, AnimationPreviewWorkspace& workspace)
    {
        Footprint footprint;
        for (const auto& [path, record] : project.Assets->Registry.Records())
            if (project.Assets->Assets.HasStore(record.Type) && project.Assets->Assets.IsResident(path, record.Type))
                ++footprint.ResidentAssets;
        footprint.Documents = workspace.Documents.size();
        const World* world = workspace.Simulation.SimulationWorld();
        footprint.PreviewEntities = world != nullptr ? world->EntityCount() : 0;
        footprint.History = workspace.Simulation.History().size();
        footprint.ContentTags = workspace.ContentTags.size();
        return footprint;
    }

    // One round of ordinary work on `rig`: play it, act, edit and take the
    // edit back, compare blends, run the lab and every scenario.
    void Round(AnimationPreviewWorkspace& workspace, const char* rig)
    {
        ASSERT_TRUE(workspace.OpenRig(rig)) << workspace.ScenarioError;
        workspace.Simulation.RunTo(90);
        AnimationScenarioAction act;
        act.Kind = AnimationScenarioActionKind::IssueRequest;
        act.Participant = "player";
        act.Intent = "Anim.Left_claw";
        act.Lifetime = AnimRequestLifetime::Impulse;
        workspace.Simulation.IssueRequest(act);
        workspace.Simulation.RunTo(200);

        const std::string behaviors = std::string(rig).replace(std::string(rig).find(".rig."), 5, ".behaviors.");
        AuthorDocument(workspace, behaviors, [](JsonValue& data) {
            JsonArrayOf(data, "behaviors").front().AsObject().emplace_back("rate", JsonValue(1.5));
        });
        workspace.Undo();
        workspace.Redo();
        workspace.Undo();

        ASSERT_TRUE(workspace.RecordTakeA());
        ASSERT_TRUE(workspace.ReplayAgainstTakeA());
        workspace.ClearTakeA();
        ASSERT_TRUE(workspace.RunLab());
        workspace.RunScenarioBatch(false);
    }
}

TEST(AnimationLongSession, RepeatedWorkHoldsSteady)
{
    AnimationTestProject project("sencha_long_session");
    project.Skeleton(kSkeleton, { { "root", -1 }, { "spine", 0 } });
    for (const char* clip : { "asset://meshes/man.blend#anim:Idle", "asset://meshes/man.blend#anim:Walk",
                              "asset://meshes/man.blend#anim:Left_claw" })
        project.Clip(clip, kSkeleton);
    project.ScanEngineAssets();
    AnimationPreviewWorkspace workspace(*project.Assets, {}, project.Root);
    std::string error;
    const std::vector<std::string> clips = { "asset://meshes/man.blend#anim:Idle", "asset://meshes/man.blend#anim:Walk",
                                             "asset://meshes/man.blend#anim:Left_claw" };
    ASSERT_TRUE(workspace.CreateRig({ .Name = "character_rig", .Clips = clips, .Preset = AnimationRigPreset::Character,
                                      .UpperBodyJoint = "spine" },
                                    error))
        << error;
    ASSERT_TRUE(workspace.CreateRig({ .Name = "simple_rig", .Clips = clips, .Preset = AnimationRigPreset::Simple,
                                      .UpperBodyJoint = {} },
                                    error))
        << error;

    // A first round opens every document and warms every cache it will use.
    Round(workspace, kCharacterRig);
    Round(workspace, kSimpleRig);
    const Footprint settled = Measure(project, workspace);
    for (int round = 0; round < 25; ++round)
    {
        Round(workspace, kCharacterRig);
        Round(workspace, kSimpleRig);
    }
    const Footprint after = Measure(project, workspace);
    EXPECT_EQ(after, settled) << "settled: " << Describe(settled) << "\nafter:   " << Describe(after);

    for (const auto& document : workspace.Documents)
        EXPECT_FALSE(document->IsEditing()) << document->VirtualPath();
    EXPECT_FALSE(workspace.CanUndo()) << "every edit was taken back";
}
