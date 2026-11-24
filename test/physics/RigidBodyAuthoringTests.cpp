#include <gtest/gtest.h>

#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/JsonArchive.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <physics/PhysicsWorld.h>
#include <physics/RigidBodyBinding.h>
#include <physics/components/Collider.h>
#include <physics/components/PhysicsBodyLink.h>
#include <physics/components/RigidBody.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/EntityPatch.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/transform/TransformComponents.h>

#include <cmath>
#include <cstring>
#include <string>
#include <string_view>

namespace
{
constexpr float kDt = 1.0f / 60.0f;

WorldComponentSchema EngineSchema(ComponentSerializerRegistry& serializers)
{
    WorldComponentSchema schema;
    ComponentRegistrar registrar(&schema, &serializers, nullptr);
    RegisterEngineComponents(registrar);
    schema.Seal();
    return schema;
}

class RigidBodyAuthoring : public ::testing::Test
{
protected:
    RigidBodyAuthoring()
        : Context(Logging)
        , Schema(EngineSchema(Serializers))
        , Runtime(Schema)
        , Bodies(Physics)
    {
        Active.Add(StoragePartitionId::Default());
    }

    World& Entities() { return Runtime.Entities(); }

    EntityId Authored(std::string_view record)
    {
        const EntityId entity = Entities().CreateEntity();
        std::string failed;
        EXPECT_TRUE(ReadEntityRecord(Entities(), entity, *JsonParse(record), Serializers, Context, &failed))
            << "the record did not read its '" << failed << "'";
        if (const LocalTransform* local = Entities().TryGet<LocalTransform>(entity))
            Entities().TryGet<WorldTransform>(entity)->Value = local->Value;
        return entity;
    }

    EntityId Floor()
    {
        return Authored(R"({"Transform": {"local": {"position": [0, -0.5, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]}},
                            "collider": {"shape": {"type": "box", "half_extents": [50, 0.5, 50]}}})");
    }

    void Tick(int steps = 1)
    {
        for (int step = 0; step < steps; ++step)
        {
            Bodies.SyncToPhysics(Entities(), Active);
            Physics.Step(kDt);
            Bodies.SyncFromPhysics(Entities(), Active);
        }
    }

    const Transform3f& Local(EntityId entity) { return Entities().TryGet<LocalTransform>(entity)->Value; }
    const RigidBody& Body(EntityId entity) { return *Entities().TryGet<RigidBody>(entity); }
    PhysicsBodyId Simulated(EntityId entity) { return Entities().TryGet<PhysicsBodyLink>(entity)->Body; }

    LoggingProvider Logging;
    SceneSerializationContext Context;
    ComponentSerializerRegistry Serializers;
    WorldComponentSchema Schema;
    RuntimeWorld Runtime;
    PhysicsWorld Physics;
    RigidBodyBinding Bodies;
    StoragePartitionSet Active;
};
} // namespace

TEST_F(RigidBodyAuthoring, TheMotionIsWrittenByNameAndABodyIsSaved)
{
    const IComponentSerializer* serializer = Serializers.FindByType(ResolveComponentTypeId<RigidBody>());
    ASSERT_NE(serializer, nullptr) << "RigidBody has no scene form";
    EXPECT_EQ(serializer->JsonKey(), "RigidBody");
    EXPECT_EQ(serializer->Persistence(), ComponentPersistence::Saved);

    const EntityId crate = Entities().CreateEntity();
    Entities().AddComponent<RigidBody>(crate, RigidBody{ .Motion = BodyMotion::Kinematic });
    JsonWriteArchive archive;
    ASSERT_TRUE(serializer->Save(archive, crate, Entities(), Context));
    const JsonValue record = archive.TakeValue();
    ASSERT_NE(record.Find("motion"), nullptr);
    EXPECT_EQ(record.Find("motion")->AsString(), "kinematic");
    for (const char* field : { "mass", "gravity_scale", "linear_damping", "angular_damping", "linear_velocity",
                               "angular_velocity", "asleep" })
        EXPECT_NE(record.Find(field), nullptr) << field;
}

TEST_F(RigidBodyAuthoring, AnAuthoredDynamicCrateFallsAndRests)
{
    (void)Floor();
    const EntityId crate = Authored(R"({"Transform": {"local": {"position": [0, 4, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]}},
                                        "collider": {"shape": {"type": "box", "half_extents": [0.5, 0.5, 0.5]}},
                                        "RigidBody": {"motion": "dynamic"}})");
    Tick(30);
    EXPECT_LT(Local(crate).Position.Y, 4.0f) << "the crate did not fall";
    Tick(300);
    EXPECT_NEAR(Local(crate).Position.Y, 0.5f, 0.05f) << "the crate did not come to rest on the floor";
    EXPECT_NEAR(Body(crate).LinearVelocity.Magnitude(), 0.0f, 0.01f);
}

TEST_F(RigidBodyAuthoring, AnAuthoredVelocityIsTheBodysFirst)
{
    const EntityId crate = Authored(R"({"Transform": {"local": {"position": [0, 4, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]}},
                                        "collider": {"shape": {"type": "box", "half_extents": [0.5, 0.5, 0.5]}},
                                        "RigidBody": {"motion": "dynamic", "gravity_scale": 0,
                                                      "linear_velocity": [3, 0, 0], "linear_damping": 0}})");
    Tick();
    EXPECT_NEAR(Local(crate).Position.X, 3.0f * kDt, 1e-3f) << "the authored velocity did not move it on its first step";
    EXPECT_NEAR(Body(crate).LinearVelocity.X, 3.0f, 1e-3f);
}

TEST_F(RigidBodyAuthoring, ABodyAuthoredAsleepDoesNotMoveUntilTouched)
{
    (void)Floor();
    const EntityId crate = Authored(R"({"Transform": {"local": {"position": [0, 2, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]}},
                                        "collider": {"shape": {"type": "box", "half_extents": [0.5, 0.5, 0.5]}},
                                        "RigidBody": {"motion": "dynamic", "asleep": true}})");
    Tick(60);
    EXPECT_FLOAT_EQ(Local(crate).Position.Y, 2.0f) << "a body authored asleep fell untouched";
    EXPECT_TRUE(Body(crate).Asleep);
    EXPECT_FALSE(Physics.IsBodyActive(Simulated(crate)));

    (void)Authored(R"({"Transform": {"local": {"position": [0, 4, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]}},
                       "collider": {"shape": {"type": "sphere", "radius": 0.25}},
                       "RigidBody": {"motion": "dynamic"}})");
    Tick(240);
    EXPECT_LT(Local(crate).Position.Y, 1.0f) << "the touched body did not wake and fall";
}

TEST_F(RigidBodyAuthoring, ASleepingBodysTransformAndBodyBytesAreUnchangedAfterAStep)
{
    (void)Floor();
    const EntityId crate = Authored(R"({"Transform": {"local": {"position": [0, 1, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]}},
                                        "collider": {"shape": {"type": "box", "half_extents": [0.5, 0.5, 0.5]}},
                                        "RigidBody": {"motion": "dynamic"}})");
    for (int step = 0; step < 600 && !Body(crate).Asleep; ++step)
        Tick();
    ASSERT_TRUE(Body(crate).Asleep) << "the resting crate never went to sleep";
    ASSERT_FALSE(Physics.IsBodyActive(Simulated(crate)));

    const LocalTransform transform = *Entities().TryGet<LocalTransform>(crate);
    const RigidBody body = Body(crate);
    Tick(5);
    EXPECT_EQ(std::memcmp(&transform, Entities().TryGet<LocalTransform>(crate), sizeof(LocalTransform)), 0);
    EXPECT_EQ(std::memcmp(&body, Entities().TryGet<RigidBody>(crate), sizeof(RigidBody)), 0);
}

TEST_F(RigidBodyAuthoring, ADynamicBodyUnderARotatedOffsetParentHasALocalTransformThatComposesToItsWorldPose)
{
    const EntityId group = Authored(
        R"({"Transform": {"local": {"position": [10, 1, -4], "rotation": [0, 0.70710678, 0, 0.70710678], "scale": [1, 1, 1]}}})");
    const Transform3f frame = Local(group);

    const EntityId crate = Authored(R"({"Transform": {"local": {"position": [2, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]}},
                                        "collider": {"shape": {"type": "box", "half_extents": [0.5, 0.5, 0.5]}},
                                        "RigidBody": {"motion": "dynamic", "gravity_scale": 0,
                                                      "linear_velocity": [0, 0, 3], "linear_damping": 0,
                                                      "angular_velocity": [0, 1, 0], "angular_damping": 0}})");
    Entities().AddComponent<Parent>(crate, Parent{ group });
    Entities().TryGet<WorldTransform>(crate)->Value = frame * Local(crate);

    Tick(10);
    const BodyTransform pose = Physics.GetBodyTransform(Simulated(crate));
    const Transform3f composed = frame * Local(crate);
    EXPECT_NEAR((composed.Position - pose.Position).Magnitude(), 0.0f, 1e-4f)
        << "the pulled local transform does not compose to where the body is";
    EXPECT_TRUE(composed.Rotation.NearlyEquals(pose.Rotation, 1e-4f));

    Bodies.EvictAll(Entities());
    const Transform3f evicted = frame * Local(crate);
    EXPECT_NEAR((evicted.Position - pose.Position).Magnitude(), 0.0f, 1e-4f)
        << "the state captured at eviction does not compose to where the body was";
}
