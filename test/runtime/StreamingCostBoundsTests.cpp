// Cost bounds for zone streaming, asserted on counted work rather than elapsed
// time so the results are identical on every machine and in every build config.
// Wall-clock numbers live in StreamingBench.Generate and
// docs/evidence/streaming-baseline.md; what belongs here is the shape of the
// work: how many row migrations an import pays, whether unload returns its
// chunk slabs, and how far a structural change's cache invalidation reaches.
//
// Three bounds below are DISABLED because the current implementation does not
// meet them yet. Each names the phase of docs/plans/unified-world-hardening.md
// that makes it pass; enabling it is that phase's gate, and it must fail before
// the fix for the reason its comment states. They are disabled rather than
// failing so a red suite always means a new regression.

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
#include <zone/ZoneLoadPackage.h>
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
ZoneLoadPackage MakeZonePackage(ZoneId zone, int entities)
{
    ZoneLoadPackage package(zone);
    for (int index = 0; index < entities; ++index)
    {
        const ZoneLocalEntityId entity = package.CreateEntity();
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
    const ZoneLoadPackage package = MakeZonePackage(ZoneId{ 1 }, kZoneEntities);

    const uint64_t before = runtime.Entities().RowMigrationCount();
    ZoneImportError error;
    ASSERT_TRUE(ImportZonePackage(
        runtime,
        schema,
        package,
        ZoneParticipation{ .Visible = true },
        &error))
        << error.Message;

    // Each entity's row is built once, at its full signature (declared
    // components plus the derived WorldTransform).
    EXPECT_EQ(runtime.Entities().RowMigrationCount() - before, 0u);
}

// Phase 4 (chunk reclamation). Today Archetype::RemoveRow leaves emptied chunks
// in place and only the last chunk per (archetype, partition) is ever reused,
// so each unload of a multi-chunk zone orphans slabs and the census climbs with
// streaming history. Enabling this test is Phase 4's gate.
TEST(StreamingCostBounds, DISABLED_StreamingChurnDoesNotGrowChunkCount)
{
    const WorldComponentSchema schema = RuntimeSchema();
    RuntimeWorld runtime(schema);
    const ZoneLoadPackage package = MakeZonePackage(ZoneId{ 1 }, kZoneEntities);

    const auto loadAndUnload = [&]
    {
        ZoneImportError error;
        ASSERT_TRUE(ImportZonePackage(
            runtime,
            schema,
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

// Phase 3 (scoped invalidation). Today every cache keys off the global
// structural counter, so a spawn in one zone rebuilds the whole world's
// propagation order — cost proportional to everything resident rather than to
// what changed. Enabling this test is Phase 3's gate.
TEST(StreamingCostBounds, DISABLED_SpawnInOneZoneDoesNotRebuildTransformOrder)
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
