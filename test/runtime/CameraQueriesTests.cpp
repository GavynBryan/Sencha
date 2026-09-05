#include <gtest/gtest.h>

#include <camera/CameraQueries.h>
#include <components/CameraComponent.h>
#include <ecs/World.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>

namespace
{
    struct CameraWorld
    {
        WorldComponentSchema Schema;
        World Entities;

        CameraWorld()
        {
            ComponentRegistrar components(&Schema, nullptr, nullptr);
            RegisterEngineComponents(components);
            Schema.Seal();
            Schema.Apply(Entities);
        }

        EntityId Camera(StoragePartitionId partition)
        {
            const EntityId entity = Entities.CreateEntity(partition);
            Entities.AddComponent<CameraComponent>(entity, CameraComponent{});
            return entity;
        }
    };
}

TEST(CameraQueries, NoCameraIsInvalid)
{
    CameraWorld world;
    (void)world.Entities.CreateEntity(PersistentStoragePartition);
    EXPECT_FALSE(FirstAuthoredCamera(world.Entities, PersistentStoragePartition).IsValid());
}

TEST(CameraQueries, TheOneCameraIsFound)
{
    CameraWorld world;
    const EntityId camera = world.Camera(PersistentStoragePartition);
    EXPECT_EQ(FirstAuthoredCamera(world.Entities, PersistentStoragePartition), camera);
}

// A camera in another partition is another level's; the query answers for the
// partition it was asked about.
TEST(CameraQueries, OnlyTheAskedPartitionCounts)
{
    CameraWorld world;
    const StoragePartitionId other{ 2 };
    const EntityId elsewhere = world.Camera(other);
    EXPECT_FALSE(FirstAuthoredCamera(world.Entities, PersistentStoragePartition).IsValid());
    EXPECT_EQ(FirstAuthoredCamera(world.Entities, other), elsewhere);
}

// A world that never registered the camera vocabulary -- a headless tool --
// has no cameras rather than an error.
TEST(CameraQueries, AWorldWithoutTheComponentHasNoCameras)
{
    World bare;
    EXPECT_FALSE(FirstAuthoredCamera(bare, PersistentStoragePartition).IsValid());
}
