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

// Counts hook calls so a write can be shown not to make any.
struct SchemaHookCounts
{
    static inline int Added = 0;
    static inline int Removed = 0;
};

struct SchemaHooked
{
    int Value = 0;
};

template <>
struct ComponentTraits<SchemaHooked>
{
    static void OnAdd(SchemaHooked&, World&, EntityId) { ++SchemaHookCounts::Added; }
    static void OnRemove(const SchemaHooked&, World&, EntityId)
    {
        ++SchemaHookCounts::Removed;
    }
};

SENCHA_DECLARE_COMPONENT_TYPE(SchemaHooked, "test.schema_hooked");
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

//=============================================================================
// SetComponentBytes
//
// The write path replication applies snapshots through. It changes a value on
// an entity that already carries the component, and does nothing else: no add,
// no remove, no lifecycle hook.
//=============================================================================

namespace
{
    std::span<const std::byte> BytesOf(const auto& value)
    {
        return { reinterpret_cast<const std::byte*>(&value), sizeof(value) };
    }

    WorldComponentSchema SealedSchema()
    {
        WorldComponentSchema schema;
        schema.Add<SchemaPosition>();
        schema.Add<SchemaVelocity>();
        schema.Add<SchemaTag>();
        schema.Add<SchemaHooked>();
        schema.Seal();
        return schema;
    }
}

TEST(WorldComponentSchemaWrite, OverwritesAComponentThatIsAlreadyThere)
{
    const WorldComponentSchema schema = SealedSchema();
    World world;
    schema.Apply(world);

    const EntityId entity = world.CreateEntity();
    world.AddComponent<SchemaPosition>(entity, { 1.0f, 2.0f });

    const SchemaPosition incoming{ 7.0f, 8.0f };
    EXPECT_TRUE(schema.SetComponentBytes(
        world, entity, ResolveComponentTypeId<SchemaPosition>(), BytesOf(incoming)));

    const SchemaPosition* stored = world.TryGet<SchemaPosition>(entity);
    ASSERT_NE(stored, nullptr);
    EXPECT_FLOAT_EQ(stored->X, 7.0f);
    EXPECT_FLOAT_EQ(stored->Y, 8.0f);
}

// A snapshot must never change an entity's shape as a side effect. If the
// component is not there, the write is refused and the caller decides -- which
// for replication means a spawn, through the typed import path.
TEST(WorldComponentSchemaWrite, RefusesToAddAComponentThatIsMissing)
{
    const WorldComponentSchema schema = SealedSchema();
    World world;
    schema.Apply(world);

    const EntityId entity = world.CreateEntity();
    const SchemaPosition incoming{ 7.0f, 8.0f };

    EXPECT_FALSE(schema.SetComponentBytes(
        world, entity, ResolveComponentTypeId<SchemaPosition>(), BytesOf(incoming)));
    EXPECT_FALSE(world.HasComponent<SchemaPosition>(entity))
        << "a refused write must not have created the component";
}

TEST(WorldComponentSchemaWrite, RefusesTheWrongNumberOfBytes)
{
    const WorldComponentSchema schema = SealedSchema();
    World world;
    schema.Apply(world);

    const EntityId entity = world.CreateEntity();
    world.AddComponent<SchemaPosition>(entity, { 1.0f, 2.0f });

    const float truncated = 5.0f;
    EXPECT_FALSE(schema.SetComponentBytes(
        world, entity, ResolveComponentTypeId<SchemaPosition>(), BytesOf(truncated)));

    const SchemaPosition* stored = world.TryGet<SchemaPosition>(entity);
    ASSERT_NE(stored, nullptr);
    EXPECT_FLOAT_EQ(stored->X, 1.0f) << "a refused write must leave the value alone";
}

TEST(WorldComponentSchemaWrite, RefusesAnUnknownComponentType)
{
    const WorldComponentSchema schema = SealedSchema();
    World world;
    schema.Apply(world);

    const EntityId entity = world.CreateEntity();
    const SchemaPosition incoming{ 7.0f, 8.0f };
    EXPECT_FALSE(schema.SetComponentBytes(
        world, entity, MakeComponentTypeId("test.not_in_this_schema"),
        BytesOf(incoming)));
}

// The reason replication refuses hooked components: a write here cannot run
// OnAdd for what arrives or OnRemove for what it replaces. This proves the
// behavior so the ban has something to point at.
TEST(WorldComponentSchemaWrite, DoesNotRunLifecycleHooks)
{
    const WorldComponentSchema schema = SealedSchema();
    World world;
    schema.Apply(world);

    SchemaHookCounts::Added = 0;
    SchemaHookCounts::Removed = 0;

    const EntityId entity = world.CreateEntity();
    world.AddComponent<SchemaHooked>(entity, { 1 });
    ASSERT_EQ(SchemaHookCounts::Added, 1);

    const SchemaHooked incoming{ 99 };
    EXPECT_TRUE(schema.SetComponentBytes(
        world, entity, ResolveComponentTypeId<SchemaHooked>(), BytesOf(incoming)));

    EXPECT_EQ(world.TryGet<SchemaHooked>(entity)->Value, 99);
    EXPECT_EQ(SchemaHookCounts::Added, 1) << "overwriting must not re-run OnAdd";
    EXPECT_EQ(SchemaHookCounts::Removed, 0)
        << "overwriting must not run OnRemove for the replaced value";
}

// A value that arrives from the network has to look like a write to every
// consumer downstream, or systems filtering on Changed<T> would miss it.
TEST(WorldComponentSchemaWrite, PublishesTheWriteToChangeDetection)
{
    const WorldComponentSchema schema = SealedSchema();
    World world;
    schema.Apply(world);

    const EntityId entity = world.CreateEntity();
    world.AddComponent<SchemaPosition>(entity, { 1.0f, 2.0f });

    world.AdvanceFrame();
    const std::uint32_t reference = world.CurrentFrame();
    world.AdvanceFrame();

    const SchemaPosition incoming{ 7.0f, 8.0f };
    ASSERT_TRUE(schema.SetComponentBytes(
        world, entity, ResolveComponentTypeId<SchemaPosition>(), BytesOf(incoming)));

    Query<Changed<SchemaPosition>> changed(world);
    int seen = 0;
    changed.ForEachChunk([&](auto& view) { seen += static_cast<int>(view.Count()); },
                         reference);
    EXPECT_EQ(seen, 1) << "a replicated write must be visible to Changed<T>";
}

// A tag has no bytes: presence is its whole value, and changing that is
// structural rather than a write.
TEST(WorldComponentSchemaWrite, ATagIsWritableOnlyAsANoOp)
{
    const WorldComponentSchema schema = SealedSchema();
    World world;
    schema.Apply(world);

    const EntityId entity = world.CreateEntity();
    const ComponentTypeId tag = ResolveComponentTypeId<SchemaTag>();

    EXPECT_FALSE(schema.SetComponentBytes(world, entity, tag, {}))
        << "a tag the entity does not carry is not writable";

    world.AddComponent<SchemaTag>(entity);
    EXPECT_TRUE(schema.SetComponentBytes(world, entity, tag, {}));

    const SchemaPosition stray{};
    EXPECT_FALSE(schema.SetComponentBytes(world, entity, tag, BytesOf(stray)))
        << "a tag carries no payload, so bytes for one are malformed";
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
