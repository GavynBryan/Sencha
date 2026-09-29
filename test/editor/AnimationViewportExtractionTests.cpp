// Drawing the viewport reads the audition and the simulation as they stand;
// only Advance moves their clocks.

#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationRigRecipe.h"

#include "AnimationTestProject.h"

#include <gtest/gtest.h>

TEST(AnimationViewportExtraction, ExtractingNeverMovesAClock)
{
    constexpr const char* kSkeleton = "asset://meshes/man.blend#skel:Man";
    constexpr const char* kWalk = "asset://meshes/man.blend#anim:Walk";
    AnimationTestProject project{ "sencha_viewport_extraction" };
    project.Skeleton(kSkeleton);
    project.Clip("asset://meshes/man.blend#anim:Idle", kSkeleton);
    project.Clip(kWalk, kSkeleton);
    project.ScanEngineAssets();
    AnimationPreviewWorkspace workspace(*project.Assets, {}, project.Root);
    std::string error;
    ASSERT_TRUE(workspace.CreateRig({ .Name = "brute", .Clips = { "asset://meshes/man.blend#anim:Idle", kWalk },
                                      .Preset = AnimationRigPreset::Simple, .UpperBodyJoint = {} },
                                    error))
        << error;
    ASSERT_TRUE(workspace.OpenRig("asset://animation/brute/brute.rig.sdata")) << workspace.Rig.Error;
    workspace.Rig.Simulation.Play();
    workspace.Advance(0.25);

    for (const AnimationViewportSource source : { AnimationViewportSource::Audition, AnimationViewportSource::Simulation })
    {
        workspace.Viewport.Source = source;
        const std::uint64_t audition = workspace.Audition.Session.Tick();
        const AnimTick simulation = workspace.Rig.Simulation.Tick();
        const std::size_t history = workspace.Rig.Simulation.History().size();
        workspace.ExtractViewport();
        const std::string note = workspace.Viewport.Note;
        workspace.ExtractViewport();
        EXPECT_EQ(workspace.Audition.Session.Tick(), audition);
        EXPECT_EQ(workspace.Rig.Simulation.Tick(), simulation);
        EXPECT_EQ(workspace.Rig.Simulation.History().size(), history);
        EXPECT_EQ(workspace.Viewport.Note, note);
    }

    const AnimTick before = workspace.Rig.Simulation.Tick();
    workspace.Advance(0.25);
    EXPECT_GT(workspace.Rig.Simulation.Tick(), before) << "Advance is what moves the clocks";
}
