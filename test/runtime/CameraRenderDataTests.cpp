#include <gtest/gtest.h>

#include <camera/CameraRig.h>
#include <components/ActiveCameraService.h>
#include <components/CameraComponent.h>
#include <ecs/World.h>
#include <render/extract/Camera.h>
#include <world/transform/TransformComponents.h>

namespace
{
    // Enough of a world to build camera render data: an active camera with a
    // projection and a world transform, and a target for it to watch.
    struct CameraDataHarness
    {
        CameraDataHarness()
        {
            WorldState.RegisterComponent<CameraComponent>();
            WorldState.RegisterComponent<CameraRig>();
            WorldState.RegisterComponent<LocalTransform>();
            WorldState.RegisterComponent<WorldTransform>();

            Target = WorldState.CreateEntity();
            WorldState.AddComponent<WorldTransform>(Target, {});

            Camera = WorldState.CreateEntity();
            WorldState.AddComponent<CameraComponent>(Camera, {});
            WorldState.AddComponent<WorldTransform>(Camera, {});
            WorldState.AddResource<ActiveCameraService>().SetActive(Camera);
        }

        void GiveCameraRig(CameraRigMode mode)
        {
            CameraRig rig{};
            rig.Mode = mode;
            rig.Target = Target;
            WorldState.AddComponent<CameraRig>(Camera, rig);
        }

        [[nodiscard]] bool Build(CameraRenderData& out)
        {
            return CameraRenderDataSystem::Build(
                WorldState.GetResource<ActiveCameraService>(),
                WorldState,
                RenderExtent{ 1280, 720 },
                out);
        }

        World WorldState;
        EntityId Camera;
        EntityId Target;
    };
}

// The exclusion is derived per frame from the rig, so the render domain learns
// which entity to skip without any component carrying a per-viewer flag.
TEST(CameraRenderDataExclusion, FirstPersonRigExcludesTheTarget)
{
    CameraDataHarness harness;
    harness.GiveCameraRig(CameraRigMode::FirstPerson);

    CameraRenderData data;
    ASSERT_TRUE(harness.Build(data));
    EXPECT_EQ(data.ExcludedEntity, harness.Target);
}

TEST(CameraRenderDataExclusion, ThirdPersonRigExcludesNothing)
{
    CameraDataHarness harness;
    harness.GiveCameraRig(CameraRigMode::ThirdPerson);

    CameraRenderData data;
    ASSERT_TRUE(harness.Build(data));
    EXPECT_FALSE(data.ExcludedEntity.IsValid());
}

// Editor viewports and authored scene cameras have no rig. They must draw
// everything rather than inherit whatever the field last held.
TEST(CameraRenderDataExclusion, CameraWithoutARigExcludesNothing)
{
    CameraDataHarness harness;

    CameraRenderData data;
    data.ExcludedEntity = EntityId{ 3, 1 };
    ASSERT_TRUE(harness.Build(data));
    EXPECT_FALSE(data.ExcludedEntity.IsValid())
        << "a rigless camera must clear a stale exclusion, not keep it";
}

// Rigs are optional vocabulary. An editor viewport and a headless render world
// never register the component at all, and building camera data must not
// require them to start doing so.
TEST(CameraRenderDataExclusion, WorldWithoutTheRigComponentStillBuilds)
{
    World world;
    world.RegisterComponent<CameraComponent>();
    world.RegisterComponent<WorldTransform>();

    const EntityId camera = world.CreateEntity();
    world.AddComponent<CameraComponent>(camera, {});
    world.AddComponent<WorldTransform>(camera, {});
    world.AddResource<ActiveCameraService>().SetActive(camera);

    CameraRenderData data;
    ASSERT_TRUE(CameraRenderDataSystem::Build(
        world.GetResource<ActiveCameraService>(),
        world,
        RenderExtent{ 1280, 720 },
        data));
    EXPECT_FALSE(data.ExcludedEntity.IsValid());
}

// Switching to third person while running has to restore the body immediately;
// the field is rebuilt every frame rather than latched at spawn.
TEST(CameraRenderDataExclusion, ModeChangeIsPickedUpOnTheNextBuild)
{
    CameraDataHarness harness;
    harness.GiveCameraRig(CameraRigMode::FirstPerson);

    CameraRenderData data;
    ASSERT_TRUE(harness.Build(data));
    ASSERT_EQ(data.ExcludedEntity, harness.Target);

    harness.WorldState.TryGet<CameraRig>(harness.Camera)->Mode =
        CameraRigMode::ThirdPerson;
    ASSERT_TRUE(harness.Build(data));
    EXPECT_FALSE(data.ExcludedEntity.IsValid());
}
