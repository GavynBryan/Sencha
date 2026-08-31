// What a component owes, and the promise that every way of building an entity
// keeps it.
//
// The point of declaring the set is that no caller has to remember it. So the
// thing worth protecting is not one path but the agreement between all of them:
// the typed add, the write into a row somebody else built, and the batch import
// that builds the row at its final signature must produce the same entity. The
// last of those reaches the set by a different route -- by id, off the sealed
// schema -- which is exactly how it could drift.

#include <ecs/CommandBuffer.h>
#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/build/EntityBuildPackage.h>
#include <world/transform/DerivedTransform.h>
#include <world/transform/TransformComponents.h>
#include <zone/ZonePackageImporter.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace
{
    struct Owner
    {
        int Value = 0;
    };

    struct FirstOwed
    {
        int Value = 5; // a member initializer, so a zeroed column is visible
    };

    // Owed by FirstOwed, not by Owner: only the closure brings it in.
    struct SecondOwed
    {
        int Value = 9;
    };

    struct OwedTag
    {
    };

    // Its own owed set names it back.
    struct SelfOwner
    {
        int Value = 0;
    };

    struct Unrelated
    {
        int Value = 0;
    };
}

SENCHA_DECLARE_COMPONENT_TYPE(Owner, "test.derived_owner");
SENCHA_DECLARE_COMPONENT_TYPE(FirstOwed, "test.derived_first");
SENCHA_DECLARE_COMPONENT_TYPE(SecondOwed, "test.derived_second");
SENCHA_DECLARE_COMPONENT_TYPE(OwedTag, "test.derived_tag");
SENCHA_DECLARE_COMPONENT_TYPE(SelfOwner, "test.derived_self");
SENCHA_DECLARE_COMPONENT_TYPE(Unrelated, "test.derived_unrelated");

template <>
struct ComponentTraits<FirstOwed>
{
    using DerivedComponents = std::tuple<SecondOwed>;
};

template <>
struct ComponentTraits<Owner>
{
    // FirstOwed twice on purpose: a set, so the duplicate is not a second add.
    using DerivedComponents = std::tuple<FirstOwed, OwedTag, FirstOwed>;
};

template <>
struct ComponentTraits<SelfOwner>
{
    using DerivedComponents = std::tuple<SelfOwner>;
};

namespace
{
    WorldComponentSchema MakeSchema()
    {
        WorldComponentSchema schema;
        schema.Add<LocalTransform>();
        schema.Add<WorldTransform>();
        schema.Add<Parent>();
        schema.Add<Owner>();
        schema.Add<FirstOwed>();
        schema.Add<SecondOwed>();
        schema.Add<OwedTag>();
        schema.Add<Unrelated>();
        schema.Seal();
        return schema;
    }

    // The whole shape of an entity: which of the schema's columns it carries,
    // and what is in the ones under test. Comparing two of these is what "the
    // same entity" means here.
    struct Shape
    {
        std::vector<bool> Carries;
        int First = 0;
        int Second = 0;

        bool operator==(const Shape&) const = default;
    };

    Shape ShapeOf(const World& world,
                  const WorldComponentSchema& schema,
                  EntityId entity)
    {
        Shape shape;
        for (const WorldComponentSchema::Entry& entry : schema.Entries())
        {
            const ComponentId id = world.GetComponentIdByType(entry.Type);
            shape.Carries.push_back(id != InvalidComponentId
                                    && world.HasComponent(entity, id));
        }
        if (const FirstOwed* first = world.TryGet<FirstOwed>(entity))
            shape.First = first->Value;
        if (const SecondOwed* second = world.TryGet<SecondOwed>(entity))
            shape.Second = second->Value;
        return shape;
    }
}

TEST(DerivedComponents, TheTypedAddProvidesTheWholeClosure)
{
    WorldComponentSchema schema = MakeSchema();
    World world;
    schema.Apply(world);

    const EntityId entity = world.CreateEntity();
    world.AddComponent<Owner>(entity, Owner{ 1 });

    EXPECT_TRUE(world.HasComponent<FirstOwed>(entity)) << "declared";
    EXPECT_TRUE(world.HasComponent<SecondOwed>(entity))
        << "owed by what was owed: the closure is transitive";
    EXPECT_TRUE(world.HasComponent<OwedTag>(entity)) << "a tag is owed like anything else";
    EXPECT_FALSE(world.HasComponent<Unrelated>(entity));

    // At their initializers, not zeroed.
    EXPECT_EQ(world.TryGet<FirstOwed>(entity)->Value, FirstOwed{}.Value);
    EXPECT_EQ(world.TryGet<SecondOwed>(entity)->Value, SecondOwed{}.Value);
}

// A component the entity already carries is not added again, whatever route
// asked for it -- which is what makes duplicates in a declared set harmless and
// what stops a cycle rather than recursing through it.
TEST(DerivedComponents, WhatIsAlreadyThereIsLeftAlone)
{
    WorldComponentSchema schema = MakeSchema();
    World world;
    schema.Apply(world);

    const EntityId entity = world.CreateEntity();
    world.AddComponent<FirstOwed>(entity, FirstOwed{ 42 });
    world.AddComponent<Owner>(entity, Owner{ 1 });

    EXPECT_EQ(world.TryGet<FirstOwed>(entity)->Value, 42)
        << "an owed component overwrote a value the caller had already set";
}

TEST(DerivedComponents, AComponentThatOwesItselfDoesNotRecurse)
{
    World world;
    world.RegisterComponent<SelfOwner>();
    const EntityId entity = world.CreateEntity();
    world.AddComponent<SelfOwner>(entity, SelfOwner{ 3 });
    EXPECT_EQ(world.TryGet<SelfOwner>(entity)->Value, 3);
}

// A world composed without the owed component is not a broken world; it is a
// world that does not know that component, exactly as it is for the derived
// transform.
TEST(DerivedComponents, AnUnregisteredOwedComponentIsSkipped)
{
    World world;
    world.RegisterComponent<Owner>();
    const EntityId entity = world.CreateEntity();
    world.AddComponent<Owner>(entity, Owner{ 1 });
    EXPECT_FALSE(world.IsRegistered<FirstOwed>());
}

// The sealed schema's by-id copy of the closure, which the import path builds
// signatures from. It has to be the same set the types declare, transitively.
TEST(DerivedComponents, TheSchemaClosureMatchesWhatTheTypesDeclare)
{
    const WorldComponentSchema schema = MakeSchema();
    const WorldComponentSchema::Entry* owner =
        schema.Find(ResolveComponentTypeId<Owner>());
    ASSERT_NE(owner, nullptr);

    std::vector<ComponentTypeId> owed = owner->Owed;
    EXPECT_EQ(owed.size(), 3u) << "the duplicate did not collapse";
    const auto holds = [&](ComponentTypeId type)
    {
        return std::find(owed.begin(), owed.end(), type) != owed.end();
    };
    EXPECT_TRUE(holds(ResolveComponentTypeId<FirstOwed>()));
    EXPECT_TRUE(holds(ResolveComponentTypeId<SecondOwed>()));
    EXPECT_TRUE(holds(ResolveComponentTypeId<OwedTag>()));

    // A component that owes nothing owes nothing transitively either.
    EXPECT_TRUE(schema.Find(ResolveComponentTypeId<Unrelated>())->Owed.empty());
}

// The one that matters: two routes to the same entity.
//
// The import builds the row at its final signature and writes the owed columns
// by id; the typed add grows the row and writes them by type. If those ever
// disagree, content and code produce different entities from the same
// description -- which is the failure this whole mechanism exists to remove.
TEST(DerivedComponents, TheImportPathAndTheTypedAddAgree)
{
    const WorldComponentSchema schema = MakeSchema();

    World typed;
    schema.Apply(typed);
    const EntityId built = typed.CreateEntity();
    typed.AddComponent<LocalTransform>(built, LocalTransform{});
    // The derived transform is the import's other obligation, and it is not
    // this mechanism; seeding it here leaves the owed closure as the only
    // difference the comparison can see.
    SeedDerivedWorldTransform(typed, built);
    typed.AddComponent<Owner>(built, Owner{ 4 });

    RuntimeWorld runtime(schema);
    EntityBuildPackage package;
    const PackageEntityId packaged = package.CreateEntity();
    ASSERT_TRUE(package.AddComponent(packaged, LocalTransform{}));
    ASSERT_TRUE(package.AddComponent(packaged, Owner{ 4 }));

    ZoneImportError error;
    ASSERT_TRUE(ImportZonePackage(runtime, schema, ZoneId{ 3 }, package,
                                  ZoneParticipation{ .Logic = true }, &error))
        << error.Message;

    std::vector<EntityId> imported;
    for (EntityId entity : runtime.Entities().GetAliveEntities())
        imported.push_back(entity);
    ASSERT_EQ(imported.size(), 1u);

    const Shape importedShape = ShapeOf(runtime.Entities(), schema, imported.front());
    const Shape builtShape = ShapeOf(typed, schema, built);
    // Named, so a mismatch says which column rather than dumping two blobs.
    const std::span<const WorldComponentSchema::Entry> entries = schema.Entries();
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        EXPECT_EQ(importedShape.Carries[i], builtShape.Carries[i])
            << entries[i].Name
            << ": the import and the typed add disagree about whether an "
               "entity built from the same components carries it";
    }
    EXPECT_EQ(importedShape, builtShape)
        << "an imported entity is not the entity the same components build in code";
}

// And that it costs one row rather than one per owed component, which is the
// only reason the import has its own route to the closure at all.
TEST(DerivedComponents, TheImportBuildsTheRowOnce)
{
    const WorldComponentSchema schema = MakeSchema();
    RuntimeWorld runtime(schema);

    EntityBuildPackage package;
    const PackageEntityId packaged = package.CreateEntity();
    ASSERT_TRUE(package.AddComponent(packaged, Owner{ 4 }));

    const std::uint64_t before = runtime.Entities().RowMigrationCount();
    ZoneImportError error;
    ASSERT_TRUE(ImportZonePackage(runtime, schema, ZoneId{ 4 }, package,
                                  ZoneParticipation{ .Logic = true }, &error))
        << error.Message;
    EXPECT_EQ(runtime.Entities().RowMigrationCount(), before)
        << "the owed columns cost archetype transitions instead of joining the "
           "signature";
}

// What a World can say about the relation on its own, without a sealed schema.
//
// The two materializations answer different questions and must not be confused:
// this one is the declared set, verbatim, which is the truthful attribution for
// a reader asking "what brought this component here". The schema's is the
// transitive closure, which is what the importer ORs into a signature before the
// row exists.
TEST(DerivedComponents, AComponentAnswersWhatItDeclares)
{
    World world;
    MakeSchema().Apply(world);

    const ComponentId owner = world.GetComponentId<Owner>();
    const std::span<const ComponentTypeId> declared = world.DeclaredOwedComponents(owner);

    // Declaration order, and the duplicate is still there: folding is the
    // schema's job, and add-if-missing is what makes a repeat harmless.
    ASSERT_EQ(declared.size(), 3u);
    EXPECT_EQ(declared[0], ResolveComponentTypeId<FirstOwed>());
    EXPECT_EQ(declared[1], ResolveComponentTypeId<OwedTag>());
    EXPECT_EQ(declared[2], ResolveComponentTypeId<FirstOwed>());

    // Not the closure: SecondOwed arrives because FirstOwed owes it, and saying
    // Owner owed it would attribute it to the wrong component.
    const auto holds = [&](ComponentTypeId type)
    {
        return std::find(declared.begin(), declared.end(), type) != declared.end();
    };
    EXPECT_FALSE(holds(ResolveComponentTypeId<SecondOwed>()));
    EXPECT_EQ(world.DeclaredOwedComponents(world.GetComponentId<FirstOwed>()).size(), 1u);

    EXPECT_TRUE(world.DeclaredOwedComponents(world.GetComponentId<Unrelated>()).empty());
    // An id this world never handed out answers nothing rather than reading
    // past its table.
    EXPECT_TRUE(world.DeclaredOwedComponents(static_cast<ComponentId>(200)).empty());
}

// The routes that address a component by id rather than by type.
//
// A command buffer is the only way to change an entity's shape while a query is
// iterating, so it is the route a gameplay system has to use -- and it erases
// the type at record time. The editor's add menu erases it too, because it is
// driven by the serializer registry and never names T. Both end in one of the
// World's two raw adds, and there are two of them because Flush coalesces a run
// of identical adds; a fix that reached only one would leave a hole that opens
// at two entities and not at one.
TEST(DerivedComponents, TheCommandBufferPathAgrees)
{
    const WorldComponentSchema schema = MakeSchema();

    World typed;
    schema.Apply(typed);
    const EntityId built = typed.CreateEntity();
    typed.AddComponent<Owner>(built, Owner{ 7 });

    World buffered;
    schema.Apply(buffered);
    const EntityId recorded = buffered.CreateEntity();
    {
        CommandBuffer commands(buffered);
        commands.AddComponent<Owner>(recorded, Owner{ 7 });
        commands.Flush();
    }

    EXPECT_EQ(ShapeOf(buffered, schema, recorded), ShapeOf(typed, schema, built));
}

// The batched half. Owner carries no OnAdd hook, so a run of identical adds is
// coalesced into AddComponentsRawBatch and never touches AddComponentRaw at
// all. Pinned, so that adding a hook to Owner later fails here rather than
// quietly turning this into a second copy of the test above.
TEST(DerivedComponents, TheCommandBufferBatchPathAgrees)
{
    static_assert(!ComponentHasOnAdd<Owner>,
                  "this test only exercises the batch path while Owner is "
                  "hook-free: Flush coalesces on the registered OnAdd hook");

    const WorldComponentSchema schema = MakeSchema();

    World typed;
    schema.Apply(typed);
    const EntityId built = typed.CreateEntity();
    typed.AddComponent<Owner>(built, Owner{ 3 });
    const Shape expected = ShapeOf(typed, schema, built);

    World buffered;
    schema.Apply(buffered);
    std::vector<EntityId> entities;
    for (int i = 0; i < 3; ++i)
        entities.push_back(buffered.CreateEntity());
    {
        CommandBuffer commands(buffered);
        for (const EntityId entity : entities)
            commands.AddComponent<Owner>(entity, Owner{ 3 });
        commands.Flush();
    }

    for (const EntityId entity : entities)
        EXPECT_EQ(ShapeOf(buffered, schema, entity), expected);
}

// The primitive itself, rather than one of its callers: the editor's add menu
// reaches it directly with bytes and an id.
TEST(DerivedComponents, TheRawAddProvidesTheDeclaredSet)
{
    const WorldComponentSchema schema = MakeSchema();

    World typed;
    schema.Apply(typed);
    const EntityId built = typed.CreateEntity();
    typed.AddComponent<Owner>(built, Owner{ 11 });

    World raw;
    schema.Apply(raw);
    const EntityId erased = raw.CreateEntity();
    const Owner value{ 11 };
    raw.AddComponentRaw(erased, raw.GetComponentId<Owner>(), &value,
                        sizeof(Owner), alignof(Owner));

    EXPECT_EQ(ShapeOf(raw, schema, erased), ShapeOf(typed, schema, built));
}

// Provisioning is add-if-missing on this route too, so an authored value the
// caller put there first is not replaced by a default one.
TEST(DerivedComponents, WhatIsAlreadyThereIsLeftAloneOnTheRawPath)
{
    World world;
    MakeSchema().Apply(world);
    const EntityId entity = world.CreateEntity();
    world.AddComponent<FirstOwed>(entity, FirstOwed{ 42 });

    const Owner value{ 1 };
    world.AddComponentRaw(entity, world.GetComponentId<Owner>(), &value,
                          sizeof(Owner), alignof(Owner));

    ASSERT_NE(world.TryGet<FirstOwed>(entity), nullptr);
    EXPECT_EQ(world.TryGet<FirstOwed>(entity)->Value, 42);
    EXPECT_TRUE(world.HasComponent<OwedTag>(entity));
}

// Every component the entity carries, which is what an authoring surface has to
// ask to show an entity's whole shape and what an undo diffs across its own add.
TEST(DerivedComponents, AnEntityReportsEveryColumnItCarries)
{
    World world;
    MakeSchema().Apply(world);
    const EntityId entity = world.CreateEntity();

    std::vector<ComponentId> ids;
    world.ComponentIdsOn(entity, ids);
    EXPECT_TRUE(ids.empty());

    world.AddComponent<Owner>(entity, Owner{ 1 });
    world.ComponentIdsOn(entity, ids);

    // Ascending id order, so a reader gets the same list twice running.
    EXPECT_TRUE(std::is_sorted(ids.begin(), ids.end()));
    const auto holds = [&](ComponentId id)
    {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    };
    EXPECT_TRUE(holds(world.GetComponentId<Owner>()));
    EXPECT_TRUE(holds(world.GetComponentId<FirstOwed>()));
    EXPECT_TRUE(holds(world.GetComponentId<SecondOwed>())); // through FirstOwed
    EXPECT_TRUE(holds(world.GetComponentId<OwedTag>()));
    EXPECT_FALSE(holds(world.GetComponentId<Unrelated>()));
    EXPECT_EQ(ids.size(), 4u);

    world.DestroyEntity(entity);
    world.ComponentIdsOn(entity, ids);
    EXPECT_TRUE(ids.empty());
}
