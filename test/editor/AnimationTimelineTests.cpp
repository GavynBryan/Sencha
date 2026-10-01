#include "EditorDocumentsFixture.h"
#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationRigRecipe.h"
#include "authoring/AnimationTimeline.h"

#include <anim/AnimationClip.h>
#include <anim/Skeleton.h>

#include "AnimationTestProject.h"

#include <gtest/gtest.h>

// The deck's clock: the audition's until a rig runs and the stage shows it,
// then the simulation's, whose past is walked rather than replayed.
namespace
{
constexpr const char* kSkeleton = "asset://meshes/man.blend#skel:Man";
constexpr const char* kIdle = "asset://meshes/man.blend#anim:Idle";
constexpr const char* kWalk = "asset://meshes/man.blend#anim:Walk";
constexpr const char* kRig = "asset://animation/timeline_rig/timeline_rig.rig.sdata";

struct AnimationTimelineTest : testing::Test
{
    AnimationTestProject Project{ "sencha_animation_timeline" };
    std::unique_ptr<EditorDocuments> Documents;
    std::unique_ptr<AnimationPreviewWorkspace> Workspace;

    void SetUp() override
    {
        Project.Skeleton(kSkeleton, { { "root", -1 }, { "spine", 0 } });
        Project.Clip(kIdle, kSkeleton);
        Project.Clip(kWalk, kSkeleton);
        Project.ScanEngineAssets();
        Documents = std::make_unique<EditorDocuments>(*Project.Assets);
        Workspace = std::make_unique<AnimationPreviewWorkspace>(*Project.Assets, Documents->Sources,
                                                                Documents->Store, std::function<void(World&)>{},
                                                                Project.Root);
    }
};
} // namespace

TEST_F(AnimationTimelineTest, WithNothingAuditionedThereIsNoClock)
{
    AnimationTimeline clock(*Workspace);
    EXPECT_TRUE(clock.Facet().Clock.empty());
}

TEST_F(AnimationTimelineTest, TheSimulationClockScrubsItsHistoryAndComesBackLive)
{
    std::string error;
    ASSERT_TRUE(Workspace->CreateRig({ .Name = "timeline_rig", .Clips = { kIdle, kWalk },
                                       .Preset = AnimationRigPreset::Simple, .UpperBodyJoint = {} },
                                     error))
        << error;
    ASSERT_TRUE(Workspace->OpenRig(kRig)) << Workspace->Rig.Error;
    Workspace->Rig.Simulation.RunTo(60);
    Workspace->Viewport.Source = AnimationViewportSource::Simulation;

    AnimationTimeline clock(*Workspace);
    ASSERT_TRUE(clock.IsSimulation());
    WorkspaceTimelineFacet facet = clock.Facet();
    EXPECT_EQ(facet.Clock, "Simulation");
    EXPECT_TRUE(facet.Live);
    EXPECT_DOUBLE_EQ(facet.Time, facet.End) << "live, the playhead is at the tick it is running";

    clock.Scrub(facet.End * 0.5);
    facet = clock.Facet();
    EXPECT_FALSE(facet.Live) << "scrubbed back, it shows a recorded tick";
    EXPECT_NEAR(facet.Time, facet.End * 0.5, Workspace->Rig.Simulation.TickSeconds());
    const AnimTick running = Workspace->Rig.Simulation.Tick();

    clock.Step(-1);
    EXPECT_LT(clock.Facet().Time, facet.Time) << "stepping back walks the history";
    EXPECT_EQ(Workspace->Rig.Simulation.Tick(), running) << "without running anything";

    clock.ReturnToLive();
    EXPECT_TRUE(clock.Facet().Live);
    clock.RunAhead();
    EXPECT_EQ(Workspace->Rig.Simulation.Tick(), running + Workspace->Rig.Simulation.Scenario().TickRate);
}

TEST_F(AnimationTimelineTest, TheAuditionClockScrubsByInspectingAMoment)
{
    SkeletonData skeleton;
    skeleton.Joints.push_back({ .Name = "root", .InverseBind = Mat4::Identity() });
    AnimationClipData clip;
    clip.SkeletonPath = "asset://preview.sskel";
    clip.DurationSeconds = 2.0f;
    clip.Tracks.push_back({ .TimesSeconds = { 0.0f, 2.0f }, .Values = { 0.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f } });
    std::string error;
    ASSERT_TRUE(Workspace->Audition.Session.SetContent("asset://preview.sskel", skeleton, clip, error)) << error;
    AnimationTimeline clock(*Workspace);
    ASSERT_FALSE(clock.IsSimulation());
    WorkspaceTimelineFacet facet = clock.Facet();
    ASSERT_EQ(facet.Clock, "Audition");
    ASSERT_GT(facet.End, 0.0);

    clock.Scrub(facet.End * 0.25);
    facet = clock.Facet();
    EXPECT_FALSE(facet.Live);
    EXPECT_NEAR(facet.Time, facet.End * 0.25, 1e-6);

    clock.TogglePlay();
    EXPECT_TRUE(clock.IsLive()) << "playing again leaves the inspected moment";
    EXPECT_TRUE(clock.IsPlaying());

    clock.ChangeSpeed(1);
    EXPECT_DOUBLE_EQ(Workspace->Audition.Session.Speed(), 2.0);
    EXPECT_DOUBLE_EQ(Workspace->Rig.Simulation.Speed(), 2.0) << "one speed for both clocks";
}
