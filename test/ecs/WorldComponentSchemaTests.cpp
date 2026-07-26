#include <ecs/Ecs.h>

#include <gtest/gtest.h>

struct SchemaPosition
{
    float X = 0.0f;
    float Y = 0.0f;
};

struct SchemaVelocity
{
    float X = 0.0f;
    float Y = 0.0f;
};

struct SchemaTag
{
};

struct SchemaCollisionA
{
    std::uint32_t Value = 0;
};

struct SchemaCollisionB
{
    std::uint64_t Value = 0;
};

SENCHA_DECLARE_COMPONENT_TYPE(SchemaPosition, "test.schema_position");
SENCHA_DECLARE_COMPONENT_TYPE(SchemaVelocity, "test.schema_velocity");
SENCHA_DECLARE_COMPONENT_TYPE(SchemaTag, "test.schema_tag");
SENCHA_DECLARE_COMPONENT_TYPE(SchemaCollisionA, "test.schema_collision");
SENCHA_DECLARE_COMPONENT_TYPE(SchemaCollisionB, "test.schema_collision");

TEST(WorldComponentSchema, AddRecordsOrderedStorageContracts)
{
    WorldComponentSchema schema;

    EXPECT_TRUE(schema.Add<SchemaPosition>());
    EXPECT_TRUE(schema.Add<SchemaTag>());
    EXPECT_FALSE(schema.Add<SchemaPosition>());

    ASSERT_EQ(schema.Size(), 2u);
    ASSERT_EQ(schema.Entries().size(), 2u);

    EXPECT_EQ(schema.Entries()[0].Type, ResolveComponentTypeId<SchemaPosition>());
    EXPECT_EQ(schema.Entries()[0].Name, "test.schema_position");
    EXPECT_EQ(schema.Entries()[0].Size, sizeof(SchemaPosition));
    EXPECT_EQ(schema.Entries()[0].Alignment, alignof(SchemaPosition));
    EXPECT_FALSE(schema.Entries()[0].IsTag);

    EXPECT_EQ(schema.Entries()[1].Type, ResolveComponentTypeId<SchemaTag>());
    EXPECT_EQ(schema.Entries()[1].Name, "test.schema_tag");
    EXPECT_EQ(schema.Entries()[1].Size, 0u);
    EXPECT_EQ(schema.Entries()[1].Alignment, 1u);
    EXPECT_TRUE(schema.Entries()[1].IsTag);

    EXPECT_TRUE(schema.Contains(ResolveComponentTypeId<SchemaPosition>()));
    EXPECT_FALSE(schema.Contains(ResolveComponentTypeId<SchemaVelocity>()));
    EXPECT_EQ(schema.RemainingCapacity(), MaxComponents - 2u);
}

TEST(WorldComponentSchema, SealAndApplyRegistersExactIds)
{
    WorldComponentSchema schema;
    schema.Add<SchemaPosition>();
    schema.Add<SchemaVelocity>();
    schema.Add<SchemaTag>();
    schema.Seal();

    ASSERT_TRUE(schema.IsSealed());

    World world;
    schema.Apply(world);

    EXPECT_EQ(world.GetComponentId<SchemaPosition>(), 0u);
    EXPECT_EQ(world.GetComponentId<SchemaVelocity>(), 1u);
    EXPECT_EQ(world.GetComponentId<SchemaTag>(), 2u);

    const EntityId entity = world.CreateEntity();
    world.AddComponent<SchemaPosition>(entity, { 4.0f, 5.0f });
    world.AddComponent<SchemaTag>(entity);

    EXPECT_TRUE(world.HasComponent<SchemaPosition>(entity));
    EXPECT_TRUE(world.HasComponent<SchemaTag>(entity));
}

TEST(WorldComponentSchema, SameSchemaProducesSameIdsInIndependentWorlds)
{
    WorldComponentSchema schema;
    schema.Add<SchemaPosition>();
    schema.Add<SchemaVelocity>();
    schema.Seal();

    World first;
    World second;
    schema.Apply(first);
    schema.Apply(second);

    EXPECT_EQ(
        first.GetComponentId<SchemaPosition>(),
        second.GetComponentId<SchemaPosition>());
    EXPECT_EQ(
        first.GetComponentId<SchemaVelocity>(),
        second.GetComponentId<SchemaVelocity>());
}

TEST(WorldComponentSchema, MatchingRegisteredPrefixIsAccepted)
{
    WorldComponentSchema schema;
    schema.Add<SchemaPosition>();
    schema.Add<SchemaVelocity>();
    schema.Seal();

    World world;
    EXPECT_EQ(world.RegisterComponent<SchemaPosition>(), 0u);

    schema.Apply(world);

    EXPECT_EQ(world.GetComponentId<SchemaPosition>(), 0u);
    EXPECT_EQ(world.GetComponentId<SchemaVelocity>(), 1u);
}

#ifndef NDEBUG
TEST(WorldComponentSchema, ApplyingBeforeSealAsserts)
{
    EXPECT_DEATH(
        {
            WorldComponentSchema schema;
            schema.Add<SchemaPosition>();
            World world;
            schema.Apply(world);
        },
        "sealed WorldComponentSchema");
}

TEST(WorldComponentSchema, AddingAfterSealAsserts)
{
    EXPECT_DEATH(
        {
            WorldComponentSchema schema;
            schema.Add<SchemaPosition>();
            schema.Seal();
            (void)schema.Add<SchemaVelocity>();
        },
        "after WorldComponentSchema::Seal");
}

TEST(WorldComponentSchema, DivergentRegistrationOrderAsserts)
{
    EXPECT_DEATH(
        {
            WorldComponentSchema schema;
            schema.Add<SchemaPosition>();
            schema.Add<SchemaVelocity>();
            schema.Seal();

            World world;
            world.RegisterComponent<SchemaVelocity>();
            schema.Apply(world);
        },
        "registration order differs");
}

TEST(WorldComponentSchema, ApplyingAfterEntityCreationAsserts)
{
    EXPECT_DEATH(
        {
            WorldComponentSchema schema;
            schema.Add<SchemaPosition>();
            schema.Seal();

            World world;
            world.CreateEntity();
            schema.Apply(world);
        },
        "registration after entity creation");
}

TEST(WorldComponentSchema, StableIdentityWithDifferentLayoutAsserts)
{
    EXPECT_DEATH(
        {
            WorldComponentSchema schema;
            schema.Add<SchemaCollisionA>();
            (void)schema.Add<SchemaCollisionB>();
        },
        "ComponentTypeId collision");
}

TEST(WorldComponentSchema, DoubleSealAsserts)
{
    EXPECT_DEATH(
        {
            WorldComponentSchema schema;
            schema.Seal();
            schema.Seal();
        },
        "already sealed");
}
#endif
