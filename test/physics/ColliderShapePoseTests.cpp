#include <gtest/gtest.h>

#include <core/json/JsonValue.h>
#include <core/serialization/JsonArchive.h>
#include <physics/CharacterMover.h>
#include <physics/PhysicsWorld.h>
#include <physics/RigidBodyBinding.h>
#include <physics/SensorOverlaps.h>
#include <physics/components/Collider.h>

#include <numbers>

// A primitive placed off its body's origin: an authored volume whose centre is
// not its entity's origin keeps the entity's transform and is still where it
// was drawn.

namespace
{
constexpr EntityId kSensor{ 4, 1 };
constexpr EntityId kActor{ 9, 1 };
}

TEST(ColliderShapePose, ASensorIsWhereItsCentreAndOrientationPlaceIt)
{
    PhysicsWorld world;
    BodyDesc sensor;
    // Two metres along the body's +X, turned so its long side runs along Z.
    sensor.Shape = CollisionShape::MakeBox(Vec3d(3.0f, 2.0f, 0.2f));
    sensor.Shape.Center = Vec3d(2.0f, 0.0f, 0.0f);
    sensor.Shape.Orientation = Quatf::FromAxisAngle(Vec3d(0, 1, 0), std::numbers::pi_v<float> / 2);
    sensor.IsTrigger = true;
    sensor.Layer = CollisionLayer::Trigger;
    sensor.UserData = PackEntity(kSensor);
    ASSERT_TRUE(world.AddBody(sensor).IsValid());

    CharacterMover mover(world, CharacterMoverConfig{}, Vec3d(0.0f, 0.0f, 0.0f));
    SensorOverlaps overlaps;
    overlaps.Begin();
    mover.ObserveSensors(kActor, overlaps);
    ASSERT_TRUE(overlaps.Commit(1));
    EXPECT_TRUE(overlaps.Pairs().empty()) << "the box sat on its body's origin";

    mover.SetPosition(Vec3d(2.0f, 0.0f, 2.5f));
    overlaps.Begin();
    mover.ObserveSensors(kActor, overlaps);
    ASSERT_TRUE(overlaps.Commit(2));
    ASSERT_EQ(overlaps.Pairs().size(), 1u) << "the box was not turned onto Z at its centre";
}

TEST(ColliderShapePose, AColliderPersistsItsShapeAndSensorFlag)
{
    Collider authored;
    authored.Shape = CollisionShape::MakeBox(Vec3d(1.0f, 0.25f, 3.0f));
    authored.Shape.Center = Vec3d(0.0f, 0.25f, -1.0f);
    authored.Shape.Orientation = Quatf::FromAxisAngle(Vec3d(0, 1, 0), 0.5f);
    authored.IsTrigger = true;

    JsonWriteArchive writer;
    WriteArchiveValue(writer, {}, authored);
    ASSERT_TRUE(writer.Ok());
    const JsonValue json = writer.TakeValue();
    ASSERT_NE(json.Find("shape"), nullptr);
    EXPECT_NE(json.Find("shape")->Find("center"), nullptr);

    Collider loaded;
    JsonReadArchive reader(json);
    ReadArchiveValue(reader, {}, loaded);
    ASSERT_TRUE(reader.Ok());
    EXPECT_EQ(loaded.Shape, authored.Shape);
    EXPECT_TRUE(loaded.IsTrigger);
    EXPECT_FALSE(loaded.Mesh.IsValid());
}
