#include "TemplateModuleRun.h"

#include <camera/CameraExclusion.h>
#include <components/ActiveCameraService.h>
#include <components/CameraComponent.h>
#include <controller/LookOrientation.h>
#include <ecs/World.h>
#include <participant/ParticipantControl.h>
#include <world/RuntimeWorld.h>
#include <world/transform/TransformComponents.h>

// The horror template's composition: one participant with a body that does not
// aim, and the view is the camera the level authored -- in the level's own
// partition, attached to nothing, excluding nothing.
#if !defined(TEST_HORROR_MODULE_PATH)
TEST(HorrorTemplate, RequiresTheHorrorModule)
{
    GTEST_SKIP() << "horror is not in SENCHA_BUILD_TEMPLATES";
}
#else
namespace
{
    struct HorrorProbe
    {
        int Participants = 0;
        bool BodyAssigned = false;
        bool BodyAims = true;
        bool CameraActive = false;
        bool CameraIsAuthored = false;
        bool CameraExcludes = true;

        void FrameUpdate(FrameUpdateContext& ctx)
        {
            const World& world = ctx.Entities;
            Participants = 0;
            EntityId body;
            if (world.IsRegistered<ParticipantControl>())
            {
                world.ForEachComponent<ParticipantControl>(
                    [&](EntityId, const ParticipantControl& control) {
                        ++Participants;
                        body = control.Body;
                    });
            }
            BodyAssigned = body.IsValid() && world.IsAlive(body);
            if (BodyAssigned)
                BodyAims = world.HasComponent<LookOrientation>(body);

            const ActiveCameraService* active = world.TryGetResource<ActiveCameraService>();
            const EntityId camera = active != nullptr ? active->GetActive() : EntityId{};
            CameraActive = camera.IsValid() && world.IsAlive(camera);
            if (!CameraActive)
                return;
            CameraIsAuthored = world.HasComponent<CameraComponent>(camera)
                && world.TryGet<Parent>(camera) == nullptr
                && world.GetEntityPartition(camera) != PersistentStoragePartition;
            CameraExcludes = world.IsRegistered<CameraExclusion>()
                && world.TryGet<CameraExclusion>(camera) != nullptr;
        }
    };
}

TEST(HorrorTemplate, TheRoomsCameraIsTheViewAndTheBodyDoesNotAim)
{
    TemplateModuleRun<HorrorProbe> run(
        TEST_HORROR_MODULE_PATH, SENCHA_REPO_ROOT "/templates/horror/assets",
        "levels/horror_room", 240);
    ASSERT_TRUE(run.Loaded());
    ASSERT_EQ(run.Exit(), 0);

    const HorrorProbe& seen = run.Seen();
    EXPECT_EQ(seen.Participants, 1);
    ASSERT_TRUE(seen.BodyAssigned) << "the prefab pawn never landed";
    EXPECT_FALSE(seen.BodyAims) << "tank controls turn the body; nothing aims";
    EXPECT_TRUE(seen.CameraActive) << "the room arrived and nothing looked through it";
    EXPECT_TRUE(seen.CameraIsAuthored) << "the view is the level's camera, not one the game made";
    EXPECT_FALSE(seen.CameraExcludes) << "a fixed camera hides nobody";
}
#endif
