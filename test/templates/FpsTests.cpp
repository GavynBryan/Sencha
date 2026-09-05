#include "TemplateModuleRun.h"

#include <camera/CameraExclusion.h>
#include <components/ActiveCameraService.h>
#include <components/CameraComponent.h>
#include <controller/LookOrientation.h>
#include <ecs/World.h>
#include <net/NetReplicationComponents.h>
#include <participant/LocalControl.h>
#include <participant/ParticipantControl.h>
#include <world/RuntimeWorld.h>
#include <world/transform/TransformComponents.h>

//=============================================================================
// The FPS template's composition, observed from inside a headless run: one
// participant with a body from the prefab, that body aimed and looked through
// from its camera child, and none of it replicated because nobody is hosting.
//=============================================================================
#if !defined(TEST_FPS_MODULE_PATH)
TEST(FpsTemplate, RequiresTheFpsModule)
{
    GTEST_SKIP() << "fps is not in SENCHA_BUILD_TEMPLATES";
}
#else
namespace
{
    struct FpsProbe
    {
        int Frames = 0;
        int Participants = 0;
        bool BodyAssigned = false;
        bool BodyAims = false;
        bool BodyReplicated = true;
        bool ParticipantReplicated = true;
        bool BodyTakesLocalLook = false;
        bool CameraIsBodysChild = false;
        bool CameraExcludesBody = false;
        bool Subject = false;

        void FrameUpdate(FrameUpdateContext& ctx)
        {
            const World& world = ctx.Entities;
            ++Frames;
            Participants = 0;
            EntityId participant;
            EntityId body;
            if (world.IsRegistered<ParticipantControl>())
            {
                world.ForEachComponent<ParticipantControl>(
                    [&](EntityId entity, const ParticipantControl& control) {
                        ++Participants;
                        participant = entity;
                        body = control.Body;
                    });
            }
            BodyAssigned = body.IsValid() && world.IsAlive(body);
            if (!BodyAssigned)
                return;

            BodyAims = world.HasComponent<LookOrientation>(body);
            BodyReplicated = world.HasComponent<NetReplicated>(body);
            ParticipantReplicated = world.HasComponent<NetReplicated>(participant);
            BodyTakesLocalLook = world.HasComponent<LocalLookControl>(body);
            Subject = LocalControlSubjectOf(world) == body;

            const ActiveCameraService* active = world.TryGetResource<ActiveCameraService>();
            const EntityId camera = active != nullptr ? active->GetActive() : EntityId{};
            if (!camera.IsValid() || !world.IsAlive(camera))
                return;
            const Parent* parent = world.TryGet<Parent>(camera);
            CameraIsBodysChild = parent != nullptr && parent->Entity == body
                              && world.HasComponent<CameraComponent>(camera);
            const CameraExclusion* exclusion = world.TryGet<CameraExclusion>(camera);
            CameraExcludesBody = exclusion != nullptr && exclusion->Excluded == body;
        }
    };
}

TEST(FpsTemplate, OnePlayerOneBodyLookedThroughFromInside)
{
    // Long enough for the prefab spawn to stage and land: it goes through the
    // async lane and settles a frame or two after the level attaches.
    TemplateModuleRun<FpsProbe> run(
        TEST_FPS_MODULE_PATH, SENCHA_REPO_ROOT "/templates/fps/assets",
        "levels/room_2", 240);
    ASSERT_TRUE(run.Loaded());
    ASSERT_EQ(run.Exit(), 0);

    const FpsProbe& seen = run.Seen();
    EXPECT_EQ(seen.Participants, 1) << "the player at this machine, and nobody else";
    ASSERT_TRUE(seen.BodyAssigned) << "the prefab pawn never landed";
    EXPECT_TRUE(seen.BodyAims) << "an FPS body carries the aim it is steered along";
    EXPECT_FALSE(seen.BodyReplicated) << "singleplayer: nothing is in the replicated table";
    EXPECT_FALSE(seen.ParticipantReplicated);
    EXPECT_TRUE(seen.Subject) << "this machine drives the body it was given";
    EXPECT_TRUE(seen.BodyTakesLocalLook) << "the driven body takes this machine's look input";
    EXPECT_TRUE(seen.CameraIsBodysChild) << "the view is the pawn prefab's camera child";
    EXPECT_TRUE(seen.CameraExcludesBody) << "looking out from inside the body excludes it";
}
#endif
