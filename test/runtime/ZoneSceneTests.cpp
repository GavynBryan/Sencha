#include <gtest/gtest.h>

#include <render/Material.h>
#include <render/MeshRendererComponent.h>
#include <render/MeshService.h>
#include <zone/ZoneScene.h>
#include <zone/ZoneRuntime.h>

TEST(ZoneScene, CreatesZoneAndRegistersDefaultStores)
{
    ZoneRuntime runtime;
    ZoneScene scene(runtime, ZoneId{ 7 }, ZoneParticipation{ .Visible = true, .Logic = true });

    EXPECT_TRUE(runtime.IsZoneLoaded(ZoneId{ 7 }));
    EXPECT_EQ(&scene.GetRegistry(), runtime.FindZone(ZoneId{ 7 }));
    EXPECT_TRUE(scene.GetRegistry().Components.Has<TransformStore<Transform3f>>());
    EXPECT_TRUE(scene.GetRegistry().Components.Has<MeshRendererStore>());
    EXPECT_TRUE(scene.GetRegistry().Components.Has<CameraStore>());

    ZoneParticipation participation = runtime.GetParticipation(ZoneId{ 7 });
    EXPECT_TRUE(participation.Visible);
    EXPECT_TRUE(participation.Logic);
}

TEST(ZoneScene, CreateEntityAddsTransformAndHierarchyRegistration)
{
    ZoneRuntime runtime;
    ZoneScene scene(runtime, ZoneId{ 1 });

    EntityId entity = scene.CreateEntity(Transform3f(
        Vec3d(1.0f, 2.0f, 3.0f), Quatf::Identity(), Vec3d::One()));

    EXPECT_TRUE(entity.IsValid());
    EXPECT_TRUE(scene.Hierarchy().IsRegistered(entity));
    ASSERT_NE(scene.Transforms().TryGetLocal(entity), nullptr);
    EXPECT_EQ(scene.Transforms().TryGetLocal(entity)->Position, Vec3d(1.0f, 2.0f, 3.0f));
}

TEST(ZoneScene, AddCameraCanSetActiveCamera)
{
    ZoneRuntime runtime;
    ZoneScene scene(runtime, ZoneId{ 1 });
    EntityId camera = scene.CreateEntity();

    EXPECT_TRUE(scene.AddCamera(camera, CameraComponent{}, true));
    EXPECT_EQ(scene.ActiveCamera().GetActive().Index, camera.Index);
}
