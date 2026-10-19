#include "AllocationCounter.h"

#include <physics/CharacterMover.h>
#include <physics/PhysicsWorld.h>
#include <physics/RigidBodyBinding.h>
#include <physics/SensorOverlaps.h>

#include <gtest/gtest.h>

TEST(SensorOverlapAllocation, FullSamplesReuseReservedStorage)
{
    SensorOverlaps overlaps(4096);
    const std::size_t before = AllocationCount();
    bool accepted = true;
    for (std::uint64_t tick = 0; tick < 20; ++tick)
    {
        overlaps.Begin();
        for (std::uint32_t i = 4096; i != 0; --i)
            accepted = overlaps.Observe({ { i, 1 }, { 5000, 1 } }, tick % 2 == 0) && accepted;
        accepted = overlaps.Commit(tick) && accepted;
    }
    const std::size_t after = AllocationCount();
    EXPECT_TRUE(accepted);
    EXPECT_EQ(before, after);
}

TEST(SensorOverlapAllocation, CachedBodyAndCharacterShapeQueriesDoNotAllocatePerSample)
{
    PhysicsWorld physics;
    BodyDesc sensor;
    sensor.IsTrigger = true;
    sensor.Layer = CollisionLayer::Trigger;
    sensor.UserData = PackEntity({ 10, 1 });
    ASSERT_TRUE(physics.AddBody(sensor).IsValid());
    CharacterMover mover(physics, CharacterMoverConfig{}, Vec3d::Zero());
    BodyDesc actor;
    actor.Motion = BodyMotion::Kinematic;
    actor.Position = Vec3d(3, 0, 0);
    actor.UserData = PackEntity({ 30, 1 });
    const auto body = physics.AddBody(actor);
    ASSERT_TRUE(body.IsValid());
    SensorOverlaps overlaps;
    overlaps.Begin();
    physics.ObserveSensors(overlaps);
    mover.ObserveSensors({ 20, 1 }, overlaps);
    ASSERT_TRUE(overlaps.Commit(0));
    const std::size_t before = AllocationCount();
    bool accepted = true;
    for (std::uint64_t tick = 1; tick <= 100; ++tick)
    {
        physics.SetBodyTransform(body, Vec3d(tick % 2 == 0 ? 3.0f : -3.0f, 0, 0), Quatf::Identity());
        overlaps.Begin();
        physics.ObserveSensors(overlaps);
        mover.ObserveSensors({ 20, 1 }, overlaps);
        accepted = overlaps.Commit(tick) && accepted;
    }
    const std::size_t after = AllocationCount();
    EXPECT_TRUE(accepted);
    EXPECT_EQ(before, after);
}
