#include "TemplateModuleRun.h"

#include <anim/AnimationClipPlaybackSystem.h>
#include <components/ActiveCameraService.h>
#include <components/CameraComponent.h>
#include <controller/LookOrientation.h>
#include <ecs/World.h>
#include <movement/components/CharacterMovement.h>
#include <participant/ParticipantControl.h>
#include <world/RuntimeWorld.h>
#include <world/transform/TransformComponents.h>

// The platformer's composition: one participant with a body that moves and
// does not aim, looked at from a camera the game made, which is not part of
// the body and follows it from outside.
#if !defined(TEST_PLATFORMER_MODULE_PATH)
TEST(PlatformerTemplate, RequiresThePlatformerModule)
{
    GTEST_SKIP() << "platformer is not in SENCHA_BUILD_TEMPLATES";
}
#else
namespace
{
    struct PlatformerProbe
    {
        Engine* Host = nullptr;
        // No starter content plays clips; the template must not pay for playback.
        bool AnimationRegistered = true;
        int Participants = 0;
        bool BodyAssigned = false;
        bool BodyMoves = false;
        bool BodyAims = true;
        bool CameraActive = false;
        bool CameraIsTheGamesOwn = false;

        void FrameUpdate(FrameUpdateContext& ctx)
        {
            AnimationRegistered = Host->Schedule().Has<AnimationClipPlaybackSystem>();
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
            if (!BodyAssigned)
                return;
            BodyMoves = world.HasComponent<CharacterMovement>(body);
            BodyAims = world.HasComponent<LookOrientation>(body);
            const ActiveCameraService* active = world.TryGetResource<ActiveCameraService>();
            const EntityId camera = active != nullptr ? active->GetActive() : EntityId{};
            CameraActive = camera.IsValid() && world.IsAlive(camera);
            if (!CameraActive)
                return;
            // Made by the game, so it lives in the persistent partition, hangs
            // off nothing, and is not the body.
            CameraIsTheGamesOwn = camera != body
                && world.HasComponent<CameraComponent>(camera)
                && world.TryGet<Parent>(camera) == nullptr
                && world.GetEntityPartition(camera) == PersistentStoragePartition;
        }
    };
}

TEST(PlatformerTemplate, ABodyThatRunsUnderAnOrbitingCamera)
{
    TemplateModuleRun<PlatformerProbe> run(
        TEST_PLATFORMER_MODULE_PATH, SENCHA_REPO_ROOT "/templates/platformer/assets",
        "levels/platformer_room", 240);
    ASSERT_TRUE(run.Loaded());
    ASSERT_EQ(run.Exit(), 0);

    const PlatformerProbe& seen = run.Seen();
    EXPECT_FALSE(seen.AnimationRegistered) << "no starter content plays clips";
    EXPECT_EQ(seen.Participants, 1);
    ASSERT_TRUE(seen.BodyAssigned) << "the prefab pawn never landed";
    EXPECT_TRUE(seen.BodyMoves);
    EXPECT_FALSE(seen.BodyAims) << "a platformer body faces where it runs; nothing aims";
    EXPECT_TRUE(seen.CameraActive);
    EXPECT_TRUE(seen.CameraIsTheGamesOwn) << "the view is the orbit camera, not part of the body";
}
#endif
