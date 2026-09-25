// Undo across a rig's documents: newest step first whichever document took
// it, the document it belongs to brought forward, the preview following, and
// no interaction left open.

#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationRigRecipe.h"

#include "AnimationAuthoringSteps.h"
#include "AnimationTestProject.h"

#include <gtest/gtest.h>

#include <string>

namespace
{
    constexpr const char* kSkeleton = "asset://meshes/man.blend#skel:Man";
    constexpr const char* kBehaviors = "asset://animation/brute/brute.behaviors.sdata";
    constexpr const char* kSlots = "asset://animation/brute/brute.slots.sdata";
    constexpr const char* kSelector = "asset://animation/brute/brute.selector.sdata";

    struct Journal
    {
        AnimationTestProject Project{ "sencha_undo_journal" };
        std::unique_ptr<AnimationPreviewWorkspace> Workspace;

        Journal()
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
        }

        std::size_t Count(const char* path, const char* key)
        {
            return Workspace->FindDocument(path)->Data()->Find(key)->AsArray().size();
        }

        std::string Active() { return Workspace->ActiveDocumentAny()->VirtualPath(); }
    };
}

TEST(AnimationUndoJournal, UndoTakesTheNewestStepInWhicheverDocumentTookIt)
{
    Journal journal;
    AnimationPreviewWorkspace& workspace = *journal.Workspace;
    EXPECT_FALSE(workspace.CanUndo()) << "creating a rig is not an edit to undo";

    AuthorDocument(workspace, kBehaviors, [](JsonValue& data) {
        JsonArrayOf(data, "behaviors").push_back(JsonObjectOf({ { "tag", JsonValue("Anim.Wave") },
                                                                { "kind", JsonValue("one_shot") } }));
    });
    AuthorDocument(workspace, kSlots, [](JsonValue& data) {
        JsonArrayOf(data, "rows").push_back(JsonObjectOf({ { "behavior", JsonValue("Anim.Wave") },
                                                           { "clip", JsonValue("asset://meshes/man.blend#anim:Walk") } }));
    });
    AuthorDocument(workspace, kSelector, [](JsonValue& data) {
        JsonArrayOf(data, "rules").push_back(JsonObjectOf({ { "name", JsonValue("wave") },
                                                            { "priority", JsonValue(5.0) },
                                                            { "enter", JsonValue(JsonValue::Array{}) },
                                                            { "behavior", JsonValue("Anim.Wave") } }));
    });
    const std::size_t behaviors = journal.Count(kBehaviors, "behaviors");
    const std::size_t rows = journal.Count(kSlots, "rows");
    const std::size_t rules = journal.Count(kSelector, "rules");

    // The author is on the behavior set; undo reaches the selector first and
    // shows it.
    ASSERT_TRUE(workspace.OpenAnimationDocument(kBehaviors));
    workspace.Undo();
    EXPECT_EQ(journal.Count(kSelector, "rules"), rules - 1);
    EXPECT_EQ(journal.Active(), kSelector);
    workspace.Undo();
    EXPECT_EQ(journal.Count(kSlots, "rows"), rows - 1);
    EXPECT_EQ(journal.Active(), kSlots);
    EXPECT_EQ(journal.Count(kBehaviors, "behaviors"), behaviors) << "an older step is still in place";

    workspace.Redo();
    EXPECT_EQ(journal.Count(kSlots, "rows"), rows);
    EXPECT_TRUE(workspace.CanRedo());

    // A new edit ends what could be redone.
    AuthorDocument(workspace, kBehaviors, [](JsonValue& data) {
        JsonArrayOf(data, "behaviors").back().AsObject().emplace_back("rate", JsonValue(2.0));
    });
    EXPECT_FALSE(workspace.CanRedo());
    workspace.Undo();
    workspace.Undo();
    workspace.Undo();
    EXPECT_EQ(journal.Count(kBehaviors, "behaviors"), behaviors - 1);
    EXPECT_FALSE(workspace.CanUndo());
    EXPECT_TRUE(workspace.Simulation.Rig()->Valid) << "the preview is back on the rig as it was created";
}

// An interaction still open when undo comes -- a drag mid-way in another
// document -- is cancelled, and undo then takes a committed step.
TEST(AnimationUndoJournal, UndoCancelsAnOpenInteractionFirst)
{
    Journal journal;
    AnimationPreviewWorkspace& workspace = *journal.Workspace;
    AuthorDocument(workspace, kSelector, [](JsonValue& data) {
        JsonArrayOf(data, "rules").front().AsObject().emplace_back("hold_min_ms", JsonValue(100.0));
    });
    ASSERT_TRUE(workspace.OpenAnimationDocument(kBehaviors));
    DataDocument& behaviors = *workspace.FindDocument(kBehaviors);
    const std::size_t count = journal.Count(kBehaviors, "behaviors");
    JsonValue dragging = behaviors.CopyRoot();
    JsonArrayOf(*dragging.Find("data"), "behaviors").clear();
    behaviors.BeginEdit();
    behaviors.PreviewRoot(std::move(dragging));

    workspace.Undo();
    EXPECT_FALSE(behaviors.IsEditing());
    EXPECT_EQ(journal.Count(kBehaviors, "behaviors"), count);
    EXPECT_EQ(workspace.FindDocument(kSelector)->Data()->Find("rules")->AsArray().front().Find("hold_min_ms"),
              nullptr)
        << "the committed selector edit was undone too";
}

// Mid-drag in the very document whose step is next: the drag is abandoned and
// the committed step undone, rather than undo spending itself on the drag and
// leaving the journal a step ahead.
TEST(AnimationUndoJournal, UndoMidInteractionInTheSameDocumentTakesTheStep)
{
    Journal journal;
    AnimationPreviewWorkspace& workspace = *journal.Workspace;
    AuthorDocument(workspace, kSelector, [](JsonValue& data) {
        JsonArrayOf(data, "rules").front().AsObject().emplace_back("hold_min_ms", JsonValue(100.0));
    });
    DataDocument& selector = *workspace.FindDocument(kSelector);
    JsonValue dragging = selector.CopyRoot();
    JsonArrayOf(*dragging.Find("data"), "rules").front().AsObject().emplace_back("cooldown_ms", JsonValue(50.0));
    selector.BeginEdit();
    selector.PreviewRoot(std::move(dragging));

    workspace.Undo();
    EXPECT_FALSE(selector.IsEditing());
    const JsonValue& rule = selector.Data()->Find("rules")->AsArray().front();
    EXPECT_EQ(rule.Find("cooldown_ms"), nullptr);
    EXPECT_EQ(rule.Find("hold_min_ms"), nullptr);
    EXPECT_FALSE(workspace.CanUndo());
}
