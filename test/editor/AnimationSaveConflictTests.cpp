// Saving every changed animation document at once: one whose file changed on
// disk meanwhile is held back and named, the rest are saved, and the conflict
// is settled either way without losing the other side's work to a mistake.

#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationRigRecipe.h"

#include "AnimationAuthoringSteps.h"
#include "AnimationTestProject.h"

#include <core/json/JsonFormat.h>

#include <gtest/gtest.h>

#include <chrono>
#include <string>

namespace
{
    constexpr const char* kSkeleton = "asset://meshes/man.blend#skel:Man";
    constexpr const char* kBehaviors = "asset://animation/brute/brute.behaviors.sdata";
    constexpr const char* kSlots = "asset://animation/brute/brute.slots.sdata";

    struct Conflict
    {
        AnimationTestProject Project{ "sencha_save_conflict" };
        std::unique_ptr<AnimationPreviewWorkspace> Workspace;

        Conflict()
        {
            Project.Skeleton(kSkeleton);
            Project.Clip("asset://meshes/man.blend#anim:Idle", kSkeleton);
            Project.Clip("asset://meshes/man.blend#anim:Walk", kSkeleton);
            Project.ScanEngineAssets();
            Workspace = std::make_unique<AnimationPreviewWorkspace>(*Project.Assets, std::function<void(World&)>{},
                                                                     Project.Root);
            std::string error;
            EXPECT_TRUE(Workspace->CreateRig({ .Name = "brute",
                                               .Clips = { "asset://meshes/man.blend#anim:Idle",
                                                          "asset://meshes/man.blend#anim:Walk" },
                                               .Preset = AnimationRigPreset::Simple, .UpperBodyJoint = {} },
                                             error))
                << error;
            // Both edited here...
            AuthorDocument(*Workspace, kBehaviors, [](JsonValue& data) {
                JsonArrayOf(data, "behaviors").front().AsObject().emplace_back("rate", JsonValue(0.5));
            });
            AuthorDocument(*Workspace, kSlots, [](JsonValue& data) {
                JsonArrayOf(data, "rows").front().AsObject().emplace_back("priority", JsonValue(3.0));
            });
            // ...and the slot map changed on disk by someone else meanwhile.
            JsonValue theirs = Project.Read("animation/brute/brute.slots.sdata");
            JsonArrayOf(*theirs.Find("data"), "rows").front().AsObject().emplace_back("priority", JsonValue(9.0));
            Project.Write("animation/brute/brute.slots.sdata", JsonFormat(theirs));
            const std::filesystem::path file = Project.Root / "animation/brute/brute.slots.sdata";
            std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) + std::chrono::seconds(5));
        }

        double SlotPriorityOnDisk()
        {
            return Project.Read("animation/brute/brute.slots.sdata").Find("data")->Find("rows")->AsArray().front()
                .Find("priority")->AsNumber();
        }
    };
}

TEST(AnimationSaveConflict, SavingAllHoldsBackOnlyTheConflict)
{
    Conflict conflict;
    AnimationPreviewWorkspace& workspace = *conflict.Workspace;
    const AnimationSaveReport report = workspace.SaveAll();
    EXPECT_EQ(report.Saved, (std::vector<std::string>{ kBehaviors }));
    EXPECT_EQ(report.Conflicts, (std::vector<std::string>{ kSlots }));
    EXPECT_TRUE(report.Failed.empty());
    EXPECT_FALSE(workspace.FindDocument(kBehaviors)->IsDirty());
    EXPECT_TRUE(workspace.FindDocument(kSlots)->IsDirty());
    EXPECT_EQ(conflict.SlotPriorityOnDisk(), 9.0) << "their change is not overwritten by Save all";
}

TEST(AnimationSaveConflict, KeepingMineWritesOverTheFile)
{
    Conflict conflict;
    AnimationPreviewWorkspace& workspace = *conflict.Workspace;
    (void)workspace.SaveAll();
    std::string error;
    ASSERT_TRUE(workspace.SaveOverFile(kSlots, error)) << error;
    EXPECT_EQ(conflict.SlotPriorityOnDisk(), 3.0);
    EXPECT_FALSE(workspace.FindDocument(kSlots)->IsDirty());
    EXPECT_TRUE(workspace.LastSave.Conflicts.empty());
}

// Taking the file's version is a step: undo brings the author's edit back,
// and the journal still undoes the other document's edit after it.
TEST(AnimationSaveConflict, TakingTheFilesIsAnUndoableStep)
{
    Conflict conflict;
    AnimationPreviewWorkspace& workspace = *conflict.Workspace;
    (void)workspace.SaveAll();
    std::string error;
    ASSERT_TRUE(workspace.AdoptFileVersion(kSlots, error)) << error;
    const auto priority = [&] {
        return workspace.FindDocument(kSlots)->Data()->Find("rows")->AsArray().front().Find("priority")->AsNumber();
    };
    EXPECT_EQ(priority(), 9.0);
    EXPECT_FALSE(workspace.FindDocument(kSlots)->IsDirty());
    EXPECT_TRUE(workspace.LastSave.Conflicts.empty());

    workspace.Undo();
    EXPECT_EQ(priority(), 3.0) << "the author's edit is back";
    workspace.Undo();
    workspace.Undo();
    EXPECT_EQ(workspace.FindDocument(kBehaviors)->Data()->Find("behaviors")->AsArray().front().Find("rate"), nullptr);
}
