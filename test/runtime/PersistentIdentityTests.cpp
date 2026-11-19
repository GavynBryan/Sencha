#include <ecs/WorldComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/identity/PersistentIdMint.h>
#include <world/identity/PersistableRef.h>
#include <world/SimulationAuthority.h>
#include <world/build/EntityBuildPackage.h>
#include <world/scene/SmapFormat.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializer.h>
#include <zone/ZonePackageImporter.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/JsonArchive.h>

#include <gtest/gtest.h>

#include <memory>
#include <tuple>
#include <vector>

struct MemberLink
{
    PersistableRef Target;
};

template <>
struct TypeSchema<MemberLink>
{
    static constexpr std::string_view Name = "test.member_link";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('M', 'L', 'N', 'K');

    static auto Fields() { return std::tuple{ MakeField("target", &MemberLink::Target) }; }
};

namespace
{
WorldComponentSchema IdentitySchema()
{
    WorldComponentSchema schema;
    schema.Add<PersistentIdComponent>();
    schema.Seal();
    return schema;
}

ZoneParticipation LiveZone()
{
    return ZoneParticipation{
        .Visible = true,
        .Physics = true,
        .Logic = true,
        .Audio = true,
    };
}

void FinishResidency(RuntimeWorld& runtime)
{
    (void)runtime.BeginResidencyProcessing();
    runtime.FinalizeResidencyProcessing();
}

PersistentEntityIndex& IndexOf(RuntimeWorld& runtime)
{
    auto* index = runtime.Entities().TryGetResource<PersistentEntityIndex>();
    EXPECT_NE(index, nullptr);
    return *index;
}
} // namespace

TEST(PersistentIdentity, RegistersOnAddAndResolves)
{
    RuntimeWorld runtime(IdentitySchema());
    RuntimeZoneRecord& zone = runtime.AttachZone(ZoneId{ 1 }, LiveZone());
    FinishResidency(runtime);

    const PersistentEntityId id{ 0x1111 };
    const EntityId entity = runtime.Entities().CreateEntity(zone.Partition);
    runtime.Entities().AddComponent<PersistentIdComponent>(entity, { id });

    EXPECT_EQ(IndexOf(runtime).TryResolve(id), entity);
    EXPECT_EQ(IndexOf(runtime).Count(), 1u);
}

TEST(PersistentIdentity, UnregistersOnEntityDestroy)
{
    RuntimeWorld runtime(IdentitySchema());
    RuntimeZoneRecord& zone = runtime.AttachZone(ZoneId{ 1 }, LiveZone());
    FinishResidency(runtime);

    const PersistentEntityId id{ 0x2222 };
    const EntityId entity = runtime.Entities().CreateEntity(zone.Partition);
    runtime.Entities().AddComponent<PersistentIdComponent>(entity, { id });
    runtime.Entities().DestroyEntity(entity);

    EXPECT_FALSE(IndexOf(runtime).TryResolve(id).IsValid());
    EXPECT_EQ(IndexOf(runtime).Count(), 0u);
}

TEST(PersistentIdentity, RemoveComponentUnregisters)
{
    RuntimeWorld runtime(IdentitySchema());
    RuntimeZoneRecord& zone = runtime.AttachZone(ZoneId{ 1 }, LiveZone());
    FinishResidency(runtime);

    const PersistentEntityId id{ 0x3333 };
    const EntityId entity = runtime.Entities().CreateEntity(zone.Partition);
    runtime.Entities().AddComponent<PersistentIdComponent>(entity, { id });
    runtime.Entities().RemoveComponent<PersistentIdComponent>(entity);

    EXPECT_FALSE(IndexOf(runtime).TryResolve(id).IsValid());
    EXPECT_TRUE(runtime.Entities().IsAlive(entity));
}

TEST(PersistentIdentity, UnsetIdStaysOutOfIndex)
{
    RuntimeWorld runtime(IdentitySchema());
    RuntimeZoneRecord& zone = runtime.AttachZone(ZoneId{ 1 }, LiveZone());
    FinishResidency(runtime);

    const EntityId entity = runtime.Entities().CreateEntity(zone.Partition);
    runtime.Entities().AddComponent<PersistentIdComponent>(entity, {});

    EXPECT_EQ(IndexOf(runtime).Count(), 0u);
    runtime.Entities().DestroyEntity(entity);
}

TEST(PersistentIdentity, FirstRegistrationWinsOnCollision)
{
    RuntimeWorld runtime(IdentitySchema());
    RuntimeZoneRecord& zone = runtime.AttachZone(ZoneId{ 1 }, LiveZone());
    FinishResidency(runtime);

    const PersistentEntityId id{ 0x4444 };
    const EntityId first = runtime.Entities().CreateEntity(zone.Partition);
    const EntityId second = runtime.Entities().CreateEntity(zone.Partition);
    runtime.Entities().AddComponent<PersistentIdComponent>(first, { id });
    runtime.Entities().AddComponent<PersistentIdComponent>(second, { id });

    EXPECT_EQ(IndexOf(runtime).TryResolve(id), first);
    EXPECT_EQ(IndexOf(runtime).CollisionCount(), 1u);

    // Destroying the collision loser must not evict the winner's entry.
    runtime.Entities().DestroyEntity(second);
    EXPECT_EQ(IndexOf(runtime).TryResolve(id), first);

    runtime.Entities().DestroyEntity(first);
    EXPECT_FALSE(IndexOf(runtime).TryResolve(id).IsValid());
}

TEST(PersistentIdentity, ZoneDetachClearsExactlyItsEntries)
{
    RuntimeWorld runtime(IdentitySchema());
    RuntimeZoneRecord& first = runtime.AttachZone(ZoneId{ 1 }, LiveZone());
    RuntimeZoneRecord& second = runtime.AttachZone(ZoneId{ 2 }, LiveZone());
    FinishResidency(runtime);

    const PersistentEntityId inFirst{ 0x5551 };
    const PersistentEntityId inSecond{ 0x5552 };
    const PersistentEntityId inPersistent{ 0x5553 };

    const EntityId firstEntity = runtime.Entities().CreateEntity(first.Partition);
    runtime.Entities().AddComponent<PersistentIdComponent>(firstEntity, { inFirst });
    const EntityId secondEntity = runtime.Entities().CreateEntity(second.Partition);
    runtime.Entities().AddComponent<PersistentIdComponent>(secondEntity, { inSecond });
    const EntityId persistentEntity =
        runtime.Entities().CreateEntity(PersistentStoragePartition);
    runtime.Entities().AddComponent<PersistentIdComponent>(
        persistentEntity, { inPersistent });

    ASSERT_TRUE(runtime.RequestDetach(ZoneId{ 1 }));
    runtime.FlushLifecycleRequests();
    FinishResidency(runtime);

    EXPECT_FALSE(IndexOf(runtime).TryResolve(inFirst).IsValid());
    EXPECT_EQ(IndexOf(runtime).TryResolve(inSecond), secondEntity);
    EXPECT_EQ(IndexOf(runtime).TryResolve(inPersistent), persistentEntity);
    EXPECT_EQ(IndexOf(runtime).Count(), 2u);
}

TEST(PersistentIdMint, MintsAreUniqueAndInTheRuntimeNamespace)
{
    World world;
    const PersistentEntityId first = MintRuntimePersistentId(world);
    const PersistentEntityId second = MintRuntimePersistentId(world);
    EXPECT_TRUE(first.IsValid());
    EXPECT_NE(first, second);
    EXPECT_FALSE(IsAuthoredPersistentEntityId(first)) << "never an id an editor could have written";
    EXPECT_NE(first.Value & PersistentEntityIdRuntimeBit, 0u);
}

TEST(PersistentIdMint, OnlyTheSimulationAuthorityMints)
{
    World world;
    world.SetResource(SimulationAuthority{ .Authoritative = false, .TickOffset = 0 });
    EXPECT_FALSE(MintRuntimePersistentId(world).IsValid());
}

TEST(PersistentIdMint, AnEntityKeepsTheIdItHasAndGainsOneOnlyOnTheAuthority)
{
    RuntimeWorld runtime(IdentitySchema());
    World& world = runtime.Entities();
    const EntityId authored = world.CreateEntity();
    world.AddComponent<PersistentIdComponent>(authored, { PersistentEntityId{ 0x4411 } });
    EXPECT_EQ(EnsurePersistentId(world, authored), PersistentEntityId{ 0x4411 });

    const EntityId spawned = world.CreateEntity();
    const PersistentEntityId minted = EnsurePersistentId(world, spawned);
    ASSERT_TRUE(minted.IsValid());
    EXPECT_EQ(IndexOf(runtime).TryResolve(minted), spawned);
    EXPECT_EQ(EnsurePersistentId(world, spawned), minted) << "a second call finds the first";

    world.SetResource(SimulationAuthority{ .Authoritative = false, .TickOffset = 0 });
    const EntityId received = world.CreateEntity();
    EXPECT_FALSE(EnsurePersistentId(world, received).IsValid());
    EXPECT_FALSE(world.HasComponent<PersistentIdComponent>(received));
}

namespace
{
struct IndexedWorld
{
    World Entities;

    IndexedWorld()
    {
        Entities.RegisterComponent<PersistentIdComponent>();
        Entities.AddResource<PersistentEntityIndex>();
    }

    EntityId Identified(std::uint64_t id)
    {
        const EntityId entity = Entities.CreateEntity();
        Entities.AddComponent<PersistentIdComponent>(entity, PersistentIdComponent{ PersistentEntityId{ id } });
        return entity;
    }
};
} // namespace

TEST(PersistableRef, AReferenceIsSavedAsItsTargetsPersistentId)
{
    IndexedWorld world;
    const EntityId chest = world.Identified(0x51);
    const PersistableRef held = MakePersistableRef(world.Entities, chest);
    EXPECT_EQ(held.Entity, chest);
    EXPECT_EQ(held.Target, PersistentEntityId{ 0x51 });

    LoggingProvider logging;
    SceneSerializationContext context{ logging };
    JsonWriteArchive writer;
    writer.BeginObject(std::string_view{});
    ASSERT_TRUE(SceneFieldCodec<PersistableRef>::Save(writer, "container", held, context));
    writer.End();
    const JsonValue saved = writer.TakeValue();
    ASSERT_NE(saved.Find("container"), nullptr);
    EXPECT_EQ(saved.Find("container")->AsString(), PersistentEntityIdToString(PersistentEntityId{ 0x51 }));

    JsonReadArchive reader(saved);
    PersistableRef loaded;
    reader.BeginObject(std::string_view{});
    ASSERT_TRUE(SceneFieldCodec<PersistableRef>::Load(reader, "container", loaded, context));
    EXPECT_EQ(loaded.Target, held.Target);
    EXPECT_FALSE(loaded.Entity.IsValid()) << "an entity handle is never persisted";
    EXPECT_EQ(ResolvePersistableRef(world.Entities, loaded), chest);
}

TEST(PersistableRef, AReferenceResolvesOnceItsTargetIsResident)
{
    IndexedWorld world;
    const PersistableRef later{ .Entity = {}, .Target = PersistentEntityId{ 0x77 } };
    EXPECT_FALSE(ResolvePersistableRef(world.Entities, later).IsValid()) << "its zone has not loaded";
    const EntityId arrived = world.Identified(0x77);
    EXPECT_EQ(ResolvePersistableRef(world.Entities, later), arrived);
}

TEST(PersistableRef, AReferenceFollowsItsTargetThroughARestream)
{
    IndexedWorld world;
    const EntityId before = world.Identified(0x90);
    PersistableRef held = MakePersistableRef(world.Entities, before);
    world.Entities.DestroyEntity(before);
    const EntityId after = world.Identified(0x90);
    EXPECT_EQ(ResolvePersistableRef(world.Entities, held), after);
    RefreshPersistableRef(world.Entities, held);
    EXPECT_EQ(held.Entity, after);
}

TEST(PersistableRef, ReadingAReferenceToAnEntityWithNoIdentityHoldsItButCannotPersistIt)
{
    IndexedWorld world;
    const EntityId bare = world.Entities.CreateEntity();
    const PersistableRef held = PersistableRefOf(world.Entities, bare);
    EXPECT_EQ(ResolvePersistableRef(world.Entities, held), bare);
    EXPECT_FALSE(held.Target.IsValid());
    EXPECT_FALSE(world.Entities.HasComponent<PersistentIdComponent>(bare));
}

TEST(PersistableRef, MakingAReferenceGivesTheEntityAnIdentityOnTheAuthority)
{
    IndexedWorld world;
    const EntityId bare = world.Entities.CreateEntity();
    const PersistableRef held = MakePersistableRef(world.Entities, bare);
    ASSERT_TRUE(held.Target.IsValid());
    EXPECT_EQ(world.Entities.TryGet<PersistentIdComponent>(bare)->Id, held.Target);
    EXPECT_EQ(MakePersistableRef(world.Entities, bare), held);

    world.Entities.SetResource(SimulationAuthority{ .Authoritative = false, .TickOffset = 0 });
    const EntityId received = world.Entities.CreateEntity();
    EXPECT_FALSE(MakePersistableRef(world.Entities, received).Target.IsValid());
}

TEST(PersistableRef, AReferenceBetweenASpawnsMembersFindsThatSpawnsMember)
{
    const WorldComponentSchema schema = [] {
        WorldComponentSchema made;
        made.Add<PersistentIdComponent>();
        made.Add<MemberLink>();
        made.Seal();
        return made;
    }();
    RuntimeWorld runtime(schema);
    ComponentSerializerRegistry serializers;
    RegisterEngineSceneSerializers(serializers);
    ASSERT_EQ(serializers.Register(std::make_unique<ComponentSerializer<MemberLink>>()),
              ComponentSerializerRegistry::RegisterResult::Added);
    LoggingProvider logging;
    SceneSerializationContext context{ logging };

    const auto identity = [](std::string_view hex) {
        return std::pair{ ResolveComponentTypeId<PersistentIdComponent>(),
                          JsonValue(JsonValue::Object{ { "id", JsonValue(std::string(hex)) } }) };
    };
    SmapEntityRecord guard;
    guard.Persistent = PersistentEntityId{ 0x6A4D };
    guard.Components = { identity("0000000000006a4d") };
    SmapEntityRecord purse;
    purse.Persistent = PersistentEntityId{ 0x9E5E };
    purse.Components = { identity("0000000000009e5e"),
                         { ResolveComponentTypeId<MemberLink>(),
                           JsonValue(JsonValue::Object{ { "target", JsonValue(std::string("0000000000006a4d")) } }) } };
    // The purse comes first, so the guard is not yet built when it is read.
    SmapContents prefab;
    prefab.Entities = { purse, guard };
    EntityBuildPackage package;
    ASSERT_TRUE(BuildEntityPackageFromSmap(prefab, serializers, package, SmapPackageOptions{ .StripAuthoredIdentity = true }));

    std::vector<std::vector<EntityId>> spawns(2);
    for (std::vector<EntityId>& spawned : spawns)
    {
        ZoneImportError error;
        ASSERT_TRUE(ImportPackageIntoPartition(runtime.Entities(), schema, package, StoragePartitionId::Default(),
                                               ZoneId{}, serializers, context, &error, &spawned))
            << error.Message;
        ASSERT_EQ(spawned.size(), 2u);
    }
    EXPECT_TRUE(context.PackageMembers.empty()) << "the members are the import's alone";
    for (const std::vector<EntityId>& spawned : spawns)
    {
        const MemberLink* link = runtime.Entities().TryGet<MemberLink>(spawned[0]);
        ASSERT_NE(link, nullptr);
        EXPECT_EQ(ResolvePersistableRef(runtime.Entities(), link->Target), spawned[1]);
        EXPECT_FALSE(link->Target.Target.IsValid()) << "the stripped member carries no id to save it by";
    }
}
