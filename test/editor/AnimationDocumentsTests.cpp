// The animation workspace's own documents: the open rig's scenario and the
// clips' events, each a document of the one journal with its place on the
// document dial, asked about before it is dropped.

#include "EditorDocumentsFixture.h"
#include "authoring/AnimationClipEventsSet.h"
#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationRigRecipe.h"
#include "authoring/AnimationTimelineLanes.h"
#include "prompts/PromptQueue.h"

#include "AnimationTestProject.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

namespace
{
constexpr const char* kSkeleton = "asset://meshes/man.blend#skel:Man";
constexpr const char* kIdle = "asset://meshes/man.blend#anim:Idle";
constexpr const char* kWalk = "asset://meshes/man.blend#anim:Walk";
constexpr const char* kFirstRig = "asset://animation/first/first.rig.sdata";
constexpr const char* kSecondRig = "asset://animation/second/second.rig.sdata";

struct AnimationDocuments : testing::Test
{
    AnimationTestProject Project{ "sencha_animation_documents" };
    std::unique_ptr<EditorDocuments> Documents;
    std::unique_ptr<AnimationPreviewWorkspace> Workspace;

    void SetUp() override
    {
        Project.Skeleton(kSkeleton, { { "root", -1 }, { "spine", 0 } });
        CookedClip(kIdle, "man.blend.anim:Idle.sanim");
        CookedClip(kWalk, "man.blend.anim:Walk.sanim");
        Project.ScanEngineAssets();
        Documents = std::make_unique<EditorDocuments>(*Project.Assets);
        Workspace = std::make_unique<AnimationPreviewWorkspace>(*Project.Assets, Documents->Sources,
                                                                Documents->Store, std::function<void(World&)>{},
                                                                Project.Root);
        std::string error;
        for (const char* name : { "first", "second" })
            ASSERT_TRUE(Workspace->CreateRig({ .Name = name, .Clips = { kIdle, kWalk },
                                               .Preset = AnimationRigPreset::Simple, .UpperBodyJoint = {} },
                                             error))
                << error;
        ASSERT_TRUE(Workspace->OpenRig(kFirstRig)) << Workspace->Rig.Error;
    }

    void TearDown() override
    {
        Workspace->Sources.DiscardAll();
        Workspace.reset();
    }

    // A clip as a cook leaves it, so its events can be authored beside its source.
    void CookedClip(const char* path, const char* cooked)
    {
        ASSERT_TRUE(Project.Assets->Registry.RegisterOrVerify(
            AssetRecord{ .Type = AssetType::AnimationClip,
                         .SourceKind = AssetSourceKind::Procedural,
                         .Path = path,
                         .FilePath = (Project.Root / ".cooked" / "meshes" / cooked).generic_string() }));
        AnimationClipData clip;
        clip.DurationSeconds = 1.0f;
        clip.SkeletonPath = kSkeleton;
        (void)Project.Assets->AnimationClips.Register(path, std::move(clip),
                                                      Project.Assets->Skeletons.AcquireOwned(kSkeleton));
        Project.Leases.push_back(Project.Assets->Assets.TryAcquireLease(path, AssetType::AnimationClip));
    }

    [[nodiscard]] std::vector<std::string> DialKeys()
    {
        WorkspaceDocumentFacet facet;
        Workspace->AppendDialDocuments(facet);
        std::vector<std::string> keys;
        for (const WorkspaceOpenDocument& open : facet.Open)
            keys.push_back(open.Key);
        return keys;
    }
};
} // namespace

TEST_F(AnimationDocuments, AnEditToTheScenarioIsAStepUndoTakesBack)
{
    SnapshotDocument& scenario = Workspace->Rig.Scenario;
    EXPECT_FALSE(scenario.IsChanged()) << "a rig opens on what its scenario file holds";
    ASSERT_TRUE(Workspace->Rig.Simulation.AddParticipant("rival"));
    EXPECT_TRUE(scenario.IsChanged());
    EXPECT_EQ(Workspace->Sources.ChangedDocuments(), std::vector<DocumentRef>{ scenario.Ref() });

    Workspace->Sources.Undo();
    EXPECT_FALSE(Workspace->Rig.Simulation.Scenario().HasParticipant("rival"));
    EXPECT_FALSE(scenario.IsChanged());
    Workspace->Sources.Redo();
    EXPECT_TRUE(Workspace->Rig.Simulation.Scenario().HasParticipant("rival"));

    ASSERT_EQ(Workspace->Sources.Save(scenario.Ref()).Status, DocumentSaveStatus::Saved);
    EXPECT_TRUE(std::filesystem::exists(scenario.Path()));
    EXPECT_FALSE(scenario.IsChanged());
}

TEST_F(AnimationDocuments, OpeningAnotherRigWithAnUnsavedScenarioAsksFirst)
{
    ASSERT_TRUE(Workspace->Rig.Simulation.AddParticipant("rival"));
    EXPECT_FALSE(Workspace->OpenRig(kSecondRig)) << "nothing settled, nothing dropped";
    EXPECT_EQ(Workspace->Rig.Path, kFirstRig);

    PromptQueue prompts;
    PromptQueue::Asker asker = prompts.MakeAsker();
    int opened = 0;
    EXPECT_TRUE(OpenRigAsking(*Workspace, asker, kSecondRig, [&] { ++opened; }));
    ASSERT_NE(prompts.Front(), nullptr) << "the open scenario has unsaved changes";
    EXPECT_EQ(prompts.Front()->Items, std::vector<std::string>{ "first.rig.sanimscenario" });
    EXPECT_TRUE(prompts.Dismiss());
    EXPECT_EQ(Workspace->Rig.Path, kFirstRig) << "kept editing";

    EXPECT_TRUE(OpenRigAsking(*Workspace, asker, kSecondRig, [&] { ++opened; }));
    EXPECT_TRUE(prompts.Answer("discard"));
    EXPECT_EQ(Workspace->Rig.Path, kSecondRig);
    EXPECT_EQ(opened, 1);
    EXPECT_TRUE(Workspace->Sources.ChangedDocuments().empty()) << "the first rig's changes were thrown away";

    EXPECT_TRUE(OpenRigAsking(*Workspace, asker, kFirstRig, [&] { ++opened; }));
    EXPECT_EQ(prompts.Front(), nullptr) << "a clean scenario is not asked about";
    EXPECT_EQ(opened, 2);
}

TEST_F(AnimationDocuments, EveryOpenClipsEventsIsOnTheDialUntilItCloses)
{
    std::string error;
    AnimationClipEventsSet& events = Workspace->ClipEvents;
    ASSERT_NE(events.OpenOrFocus(kIdle, error), nullptr) << error;
    ASSERT_NE(events.OpenOrFocus(kWalk, error), nullptr) << error;
    AnimationClipEventsDocument& idle = *events.Find(kIdle);
    AnimationClipEvent step;
    step.Time = 0.5f;
    step.Binding = "anim.event";
    (void)events.Add(idle, step);

    const std::vector<std::string> keys = DialKeys();
    EXPECT_NE(std::ranges::find(keys, std::string(kEventsDialKey) + kIdle), keys.end())
        << "an earlier clip's events stay reachable once another is auditioned";
    EXPECT_NE(std::ranges::find(keys, std::string(kEventsDialKey) + kWalk), keys.end());
    EXPECT_EQ(Workspace->DialKeyOf(events.RefOf(idle)), std::string(kEventsDialKey) + kIdle);
    EXPECT_EQ(Workspace->DialKeyOf(Workspace->Rig.Scenario.Ref()),
              std::string(kScenarioDialKey) + Workspace->Rig.Scenario.Ref().Key);

    EXPECT_FALSE(events.Close(kIdle, DirtyDisposition::Refuse, error)) << "changed events are not dropped unasked";
    EXPECT_NE(events.Find(kIdle), nullptr);
    EXPECT_TRUE(events.Close(kIdle, DirtyDisposition::Discard, error)) << error;
    EXPECT_TRUE(events.Close(kWalk, DirtyDisposition::Refuse, error)) << "unchanged events close at once";
    EXPECT_EQ(events.Find(kIdle), nullptr);
    EXPECT_EQ(DialKeys().size(), 1u) << "only the scenario is left";
}

TEST_F(AnimationDocuments, ARetimeIsALiveEditACancelPutsBack)
{
    std::string error;
    AnimationClipEventsSet& events = Workspace->ClipEvents;
    AnimationClipEventsDocument* idle = events.OpenOrFocus(kIdle, error);
    ASSERT_NE(idle, nullptr) << error;
    AnimationClipEvent step;
    step.Time = 0.25f;
    step.Binding = "anim.event";
    const std::uint32_t key = events.Add(*idle, step);

    events.PreviewTime(*idle, key, 0.75);
    EXPECT_TRUE(idle->IsEditing());
    EXPECT_FLOAT_EQ(idle->Find(key)->Time, 0.75f);
    events.CancelEdit(*idle);
    EXPECT_FALSE(idle->IsEditing());
    EXPECT_FLOAT_EQ(idle->Find(key)->Time, 0.25f);

    const std::vector<TimelineMark> marks = AnimationEventMarks(*idle, 2.0, key);
    ASSERT_EQ(marks.size(), 1u);
    EXPECT_DOUBLE_EQ(marks[0].Time, 0.5);
    EXPECT_TRUE(marks[0].Selected && marks[0].Movable);
    EXPECT_EQ(ParseAnimationEventMarkKey(marks[0].Key), key) << "a mark's key names its event";
    EXPECT_FALSE(ParseAnimationEventMarkKey("rule:0:1").has_value());

    events.PreviewTime(*idle, key, 0.6);
    events.CommitEdit(*idle);
    EXPECT_FLOAT_EQ(idle->Find(key)->Time, 0.6f);
    events.Remove(*idle, key);
    EXPECT_EQ(idle->Find(key), nullptr);
    Workspace->Sources.Undo();
    EXPECT_NE(idle->Find(key), nullptr) << "each change through the set is a step";
}
