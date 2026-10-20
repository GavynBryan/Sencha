#include <gtest/gtest.h>

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/BinaryArchive.h>
#include <core/serialization/JsonArchive.h>
#include <physics/components/Collider.h>
#include <world/serialization/ComponentSerializer.h>

#include <sstream>

namespace
{
class ColliderSerializationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Serializer.RegisterStorage(Scene);
        Entity = Scene.Components.CreateEntity();
    }
    bool Load(const JsonValue& value)
    {
        JsonReadArchive archive(value);
        SceneSerializationContext context(Logging);
        return Serializer.Load(archive, Entity, Scene, context);
    }
    Registry Scene;
    ComponentSerializer<Collider> Serializer;
    EntityId Entity;
    LoggingProvider Logging;
};
}

TEST_F(ColliderSerializationTest, MissingFieldsHaveIdentityPoseAndPrimitiveDefaults)
{
    ASSERT_TRUE(Load(JsonValue::Object{}));
    const auto* collider = Scene.Components.TryGet<Collider>(Entity);
    ASSERT_NE(collider, nullptr);
    EXPECT_EQ(collider->Shape, CollisionShape{});
    EXPECT_EQ(collider->ShapePose, (BodyTransform{ {}, Quatf::Identity() }));
    EXPECT_FALSE(collider->IsTrigger);
    EXPECT_FALSE(collider->Mesh.IsValid());
}

TEST_F(ColliderSerializationTest, NormalJsonAndLegacyBinaryPreserveEveryPrimitiveAndShapePose)
{
    for (const auto shape : { CollisionShape::MakeSphere(0.7f), CollisionShape::MakeBox({ 1, 2, 3 }),
                             CollisionShape::MakeCapsule(0.4f, 1.2f) })
    {
        Collider collider;
        collider.Shape = shape;
        collider.IsTrigger = true;
        collider.ShapePose = { { 3, -2, 5 }, Quatf::FromAxisAngle(Vec3d::Up(), 0.7f) };
        if (Scene.Components.HasComponent<Collider>(Entity))
            Scene.Components.RemoveComponent<Collider>(Entity);
        Scene.Components.AddComponent(Entity, collider);
        SceneSerializationContext context(Logging);
        JsonWriteArchive out;
        ASSERT_TRUE(Serializer.Save(out, Entity, Scene, context));
        ASSERT_TRUE(Load(out.TakeValue()));
        EXPECT_EQ(Scene.Components.TryGet<Collider>(Entity)->Shape, shape);
        EXPECT_EQ(Scene.Components.TryGet<Collider>(Entity)->ShapePose, collider.ShapePose);
        EXPECT_TRUE(Scene.Components.TryGet<Collider>(Entity)->IsTrigger);

        std::stringstream bytes(std::ios::in | std::ios::out | std::ios::binary);
        BinaryWriter writer(bytes);
        BinaryWriteArchive binaryOut(writer);
        ASSERT_TRUE(Serializer.Save(binaryOut, Entity, Scene, context));
        bytes.seekg(0);
        BinaryReader reader(bytes);
        BinaryReadArchive binaryIn(reader);
        ASSERT_TRUE(Serializer.Load(binaryIn, Entity, Scene, context));
        EXPECT_EQ(Scene.Components.TryGet<Collider>(Entity)->ShapePose, collider.ShapePose);
    }
}

TEST_F(ColliderSerializationTest, InvalidShapesAndUnsupportedMeshSourcesDoNotInstallAComponent)
{
    for (const char* text : { R"({"shape":{"type":"Box","half_extents":{"x":0,"y":1,"z":1}}})",
                             R"({"shape":{"type":"Unknown"}})",
                             R"({"shape_pose":{"rotation":{"x":0,"y":0,"z":0,"w":0}}})",
                             R"({"mesh":"asset://unsupported.scol"})" })
    {
        const auto source = JsonParse(text);
        ASSERT_TRUE(source);
        EXPECT_FALSE(Load(*source)) << text;
        EXPECT_FALSE(Scene.Components.HasComponent<Collider>(Entity));
    }
}

TEST_F(ColliderSerializationTest, RuntimeMeshHandlesAndInvalidPosesCannotBeSilentlySavedAsBoxes)
{
    Collider collider;
    collider.Mesh = CollisionShapeHandle{ 9 };
    Scene.Components.AddComponent(Entity, collider);
    SceneSerializationContext context(Logging);
    JsonWriteArchive out;
    EXPECT_FALSE(Serializer.Save(out, Entity, Scene, context));
    EXPECT_FALSE(out.Ok());
    Scene.Components.TryGet<Collider>(Entity)->Mesh = {};
    Scene.Components.TryGet<Collider>(Entity)->ShapePose.Rotation = {};
    Scene.Components.TryGet<Collider>(Entity)->ShapePose.Rotation.W = 0;
    JsonWriteArchive invalidPose;
    EXPECT_FALSE(Serializer.Save(invalidPose, Entity, Scene, context));
}
