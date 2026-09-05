#include "TemplateModuleRun.h"

#include <components/ActiveCameraService.h>
#include <ecs/World.h>
#include <movement/MovementTags.h>
#include <participant/LocalControl.h>
#include <participant/ParticipantControl.h>
#include <world/RuntimeWorld.h>

//=============================================================================
// The blank template: an empty game module and one empty scene. The engine
// boots it, loads the scene, runs frames, and shuts down, and nothing that a
// player-facing game would install is there -- which is what makes it the
// floor every other template stands on.
//=============================================================================
#if !defined(TEST_BLANK_MODULE_PATH)
TEST(BlankTemplate, RequiresTheBlankModule)
{
    GTEST_SKIP() << "blank is not in SENCHA_BUILD_TEMPLATES";
}
#else
namespace
{
    struct BlankProbe
    {
        int Frames = 0;
        int Participants = 0;
        bool ActiveCamera = false;
        bool MovementVocabulary = false;
        bool LocalSubject = false;

        void FrameUpdate(FrameUpdateContext& ctx)
        {
            const World& world = ctx.Entities;
            ++Frames;
            Participants = 0;
            if (world.IsRegistered<ParticipantControl>())
            {
                world.ForEachComponent<ParticipantControl>(
                    [&](EntityId, const ParticipantControl&) { ++Participants; });
            }
            const ActiveCameraService* camera =
                world.TryGetResource<ActiveCameraService>();
            ActiveCamera = camera != nullptr && camera->HasActive();
            MovementVocabulary = world.TryGetResource<MovementTags>() != nullptr;
            LocalSubject = LocalControlSubjectOf(world).IsValid();
        }
    };
}

TEST(BlankTemplate, BootsLoadsItsSceneAndInstallsNothing)
{
    TemplateModuleRun<BlankProbe> run(
        TEST_BLANK_MODULE_PATH, SENCHA_REPO_ROOT "/templates/blank/assets",
        "levels/empty", 60);
    ASSERT_TRUE(run.Loaded());

    EXPECT_EQ(run.Exit(), 0);
    // The exit frame is counted before its update runs, so the probe sees one
    // fewer frame than the limit names. The claim is that it ran, not how many.
    EXPECT_GE(run.Seen().Frames, 30);
    EXPECT_EQ(run.Seen().Participants, 0) << "nobody was admitted; nothing asked";
    EXPECT_FALSE(run.Seen().ActiveCamera) << "no camera is active; nothing chose one";
    EXPECT_FALSE(run.Seen().MovementVocabulary)
        << "movement is opt-in and blank did not opt in";
    EXPECT_FALSE(run.Seen().LocalSubject);
}
#endif
