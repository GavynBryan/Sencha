// Cost bounds for zone streaming, asserted on counted work rather than elapsed
// time so the results are identical on every machine and in every build config.
// Wall-clock numbers live in StreamingBench.Generate and
// docs/evidence/streaming-baseline.md; what belongs here is the shape of the
// work: how many row migrations an import pays, whether unload returns its
// chunk slabs, and how far a structural change's cache invalidation reaches.
//
// Every bound here is live. Each was written against a cost the implementation
// did not yet meet, disabled with the phase of
// docs/plans/unified-world-hardening.md that would reach it, and enabled as that
// phase landed — after confirming it failed beforehand for the reason its comment
// states.

#include <gtest/gtest.h>

#include <ecs/ArchetypeSignature.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <render/PointLightComponent.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/transform/PropagationOrderCache.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformPropagation.h>
#include <world/build/EntityBuildPackage.h>
#include <zone/ZonePackageImporter.h>

#include <cstdint>

namespace
{
constexpr int kZoneEntities = 200;

WorldComponentSchema RuntimeSchema()
{
    WorldComponentSchema schema;
    RegisterEngineRuntimeComponents(schema);
    schema.Seal();
    return schema;
}

// Three components per entity, one of which (LocalTransform) seeds a derived
// WorldTransform on import — so the incremental path pays for that seeding too.
EntityBuildPackage MakeZonePackage(int entities)
{
    EntityBuildPackage package;
    for (int index = 0; index < entities; ++index)
    {
        const PackageEntityId entity = package.CreateEntity();
        Transform3f transform;
        transform.Position = Vec3d(static_cast<float>(index), 0.0f, 0.0f);
        package.AddComponent(entity, LocalTransform{ transform });
        PointLightComponent light{};
        light.Intensity = 1.0f;
        light.Range = 4.0f;
        package.AddComponent(entity, light);
    }
    return package;
}

EntityId SpawnTransformEntity(
    World& world,
    StoragePartitionId partition,
    const Vec3d& position)
{
    Transform3f transform;
    transform.Position = position;
    const EntityId entity = world.CreateEntity(partition);
    world.AddComponent<LocalTransform>(entity, LocalTransform{ transform });
    world.AddComponent<WorldTransform>(entity, WorldTransform{ transform });
    return entity;
}

StoragePartitionSet SetOf(StoragePartitionId partition)
{
    StoragePartitionSet partitions;
    partitions.Add(StoragePartitionId::Default());
    partitions.Add(partition);
    return partitions;
}
} // namespace

// ── Storage mechanism the batch import path depends on ───────────────────────

// Creating a row at its final signature costs no row migration, while adding
// the same components one at a time costs one per component. This is the whole
// basis of the batched-import fix, so it is asserted directly on World: if this
// property ever stops holding, the import bound below becomes unreachable.
TEST(StreamingCostBounds, FinalSignatureCreationCostsNoRowMigrations)
{
    World world;
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<WorldTransform>();
    world.RegisterComponent<PointLightComponent>();

    ArchetypeSignature signature;
    signature.set(world.GetComponentId<LocalTransform>());
    signature.set(world.GetComponentId<WorldTransform>());
    signature.set(world.GetComponentId<PointLightComponent>());

    const uint64_t beforeBySignature = world.RowMigrationCount();
    const EntityId built = world.CreateEntityWithSignature(
        StoragePartitionId::Default(),
        signature);
    *world.TryGet<LocalTransform>(built) = LocalTransform{};
    *world.TryGet<WorldTransform>(built) = WorldTransform{};
    *world.TryGet<PointLightComponent>(built) = PointLightComponent{};
    EXPECT_EQ(world.RowMigrationCount() - beforeBySignature, 0u);

    const uint64_t beforeIncremental = world.RowMigrationCount();
    const EntityId grown = world.CreateEntity();
    world.AddComponent<LocalTransform>(grown, LocalTransform{});
    world.AddComponent<WorldTransform>(grown, WorldTransform{});
    world.AddComponent<PointLightComponent>(grown, PointLightComponent{});
    EXPECT_EQ(world.RowMigrationCount() - beforeIncremental, 3u);
}

// ── Invalidation blast radius: the inverse guard ─────────────────────────────

// Cross-partition parenting is legal, so the propagation order is genuinely
// world-global and a hierarchy change in ANY partition must rebuild it. This
// test exists so the scoped-invalidation work cannot satisfy its own bound by
// simply invalidating less than correctness requires. It is live from the
// start and must stay live.
TEST(StreamingCostBounds, CrossPartitionParentChangeRebuildsTransformOrder)
{
    constexpr StoragePartitionId zoneA{ 1 };
    constexpr StoragePartitionId zoneB{ 2 };

    const WorldComponentSchema schema = RuntimeSchema();
    RuntimeWorld runtime(schema);
    World& world = runtime.Entities();
    world.AddResource<PropagationOrderCache>();

    const EntityId parent = SpawnTransformEntity(world, zoneA, Vec3d(1.0f, 0.0f, 0.0f));
    const EntityId child = SpawnTransformEntity(world, zoneB, Vec3d(0.0f, 0.0f, 0.0f));

    StoragePartitionSet both;
    both.Add(StoragePartitionId::Default());
    both.Add(zoneA);
    both.Add(zoneB);

    PropagateTransforms(world, both, TransformPropagationDomain::Simulation);
    const uint64_t afterFirstSweep =
        world.GetResource<PropagationOrderCache>().RebuildCount();

    // Re-parent across a partition boundary.
    world.AddComponent<Parent>(child, Parent{ parent });
    PropagateTransforms(world, both, TransformPropagationDomain::Simulation);

    EXPECT_GT(world.GetResource<PropagationOrderCache>().RebuildCount(),
              afterFirstSweep)
        << "a cross-partition hierarchy change must rebuild the global order";

    // And the child must actually inherit the parent's world position.
    const WorldTransform* childWorld = world.TryGet<WorldTransform>(child);
    ASSERT_NE(childWorld, nullptr);
    EXPECT_FLOAT_EQ(childWorld->Value.Position.X, 1.0f);
}

// ── Bounds the hardening phases must reach ──────────────────────────────────

// Phase 2 (batch import). The importer builds each entity's row once at its
// final archetype signature and writes every column in place, so no row
// migrates during import.
TEST(StreamingCostBounds, ImportPerformsNoRowMigrationsPerEntity)
{
    const WorldComponentSchema schema = RuntimeSchema();
    RuntimeWorld runtime(schema);
    const EntityBuildPackage package = MakeZonePackage(kZoneEntities);

    const uint64_t before = runtime.Entities().RowMigrationCount();
    ZoneImportError error;
    ASSERT_TRUE(ImportZonePackage(
        runtime,
        schema,
        ZoneId{ 1 },
        package,
        ZoneParticipation{ .Visible = true },
        &error))
        << error.Message;

    // Each entity's row is built once, at its full signature (declared
    // components plus the derived WorldTransform).
    EXPECT_EQ(runtime.Entities().RowMigrationCount() - before, 0u);
}

// The split that makes the bound above reachable: a structural change relocates
// rows, so cached row addresses must be re-resolved, but it does not change who
// is whose parent. Re-resolving costs the order; rebuilding costs a full
// traversal, so the two must not be driven by the same signal.
TEST(StreamingCostBounds, FlatSpawnResolvesAddressesWithoutRebuildingOrder)
{
    constexpr StoragePartitionId zone{ 1 };

    const WorldComponentSchema schema = RuntimeSchema();
    RuntimeWorld runtime(schema);
    World& world = runtime.Entities();
    world.AddResource<PropagationOrderCache>();

    const EntityId parent = SpawnTransformEntity(world, zone, Vec3d(5.0f, 0.0f, 0.0f));
    const EntityId child = SpawnTransformEntity(world, zone, Vec3d(1.0f, 0.0f, 0.0f));
    world.AddComponent<Parent>(child, Parent{ parent });

    const StoragePartitionSet partitions = SetOf(zone);
    world.AdvanceFrame();
    PropagateTransforms(world, partitions, TransformPropagationDomain::Simulation);

    const PropagationOrderCache& cache =
        world.GetResource<PropagationOrderCache>();
    const uint64_t rebuilds = cache.RebuildCount();
    const uint64_t resolves = cache.AddressResolveCount();

    world.AdvanceFrame();
    SpawnTransformEntity(world, zone, Vec3d(0.0f, 7.0f, 0.0f));
    PropagateTransforms(world, partitions, TransformPropagationDomain::Simulation);

    EXPECT_EQ(cache.RebuildCount(), rebuilds)
        << "a flat spawn changes no hierarchy";
    EXPECT_GT(cache.AddressResolveCount(), resolves)
        << "but it can relocate rows, so addresses must be re-resolved";

    // The child still tracks its parent afterwards, so the addresses that were
    // re-resolved are the right ones.
    world.AdvanceFrame();
    world.TryGet<LocalTransform>(parent)->Value.Position = Vec3d(6.0f, 0.0f, 0.0f);
    PropagateTransforms(world, partitions, TransformPropagationDomain::Simulation);
    EXPECT_FLOAT_EQ(world.TryGet<WorldTransform>(child)->Value.Position.X, 7.0f);
}

// Phase 4 (chunk reclamation). A slab that loses its last row returns to the
// archetype's free list, so ten load/unload cycles hold the memory of one. Before
// that, only the last chunk per (archetype, partition) was ever reused and every
// other slab of an unloaded zone was orphaned, so the census climbed with
// streaming history.
TEST(StreamingCostBounds, StreamingChurnDoesNotGrowChunkCount)
{
    const WorldComponentSchema schema = RuntimeSchema();
    RuntimeWorld runtime(schema);
    const EntityBuildPackage package = MakeZonePackage(kZoneEntities);

    const auto loadAndUnload = [&]
    {
        ZoneImportError error;
        ASSERT_TRUE(ImportZonePackage(
            runtime,
            schema,
            ZoneId{ 1 },
            package,
            ZoneParticipation{ .Visible = true },
            &error))
            << error.Message;
        ASSERT_TRUE(runtime.RequestDetach(ZoneId{ 1 }));
        runtime.FlushLifecycleRequests();
        (void)runtime.BeginResidencyProcessing();
        runtime.FinalizeResidencyProcessing();
    };

    loadAndUnload();
    const size_t afterFirstCycle = runtime.Entities().ChunkCount();

    for (int cycle = 0; cycle < 9; ++cycle)
        loadAndUnload();

    EXPECT_EQ(runtime.Entities().ChunkCount(), afterFirstCycle)
        << "unload must return its chunk slabs; "
        << runtime.Entities().EmptyChunkCount() << " empty chunks retained";
}

// Phase 3 (scoped invalidation). The order covers parented entities and is
// invalidated by hierarchy change, not by the global structural counter, so a
// flat spawn anywhere leaves it standing.
TEST(StreamingCostBounds, SpawnInOneZoneDoesNotRebuildTransformOrder)
{
    constexpr StoragePartitionId active{ 1 };
    constexpr StoragePartitionId dormant{ 2 };

    const WorldComponentSchema schema = RuntimeSchema();
    RuntimeWorld runtime(schema);
    World& world = runtime.Entities();
    world.AddResource<PropagationOrderCache>();

    for (int index = 0; index < kZoneEntities; ++index)
    {
        SpawnTransformEntity(world, active, Vec3d(static_cast<float>(index), 0.0f, 0.0f));
        SpawnTransformEntity(world, dormant, Vec3d(0.0f, static_cast<float>(index), 0.0f));
    }

    const StoragePartitionSet activeOnly = SetOf(active);
    PropagateTransforms(world, activeOnly, TransformPropagationDomain::Simulation);
    const uint64_t settled =
        world.GetResource<PropagationOrderCache>().RebuildCount();

    // A flat spawn into the dormant partition changes no hierarchy and touches
    // no active-partition row.
    SpawnTransformEntity(world, dormant, Vec3d(0.0f, 0.0f, 1.0f));
    PropagateTransforms(world, activeOnly, TransformPropagationDomain::Simulation);

    EXPECT_EQ(world.GetResource<PropagationOrderCache>().RebuildCount(), settled)
        << "a spawn outside the propagated partitions must not rebuild the order";
}
