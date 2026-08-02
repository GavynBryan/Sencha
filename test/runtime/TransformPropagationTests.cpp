#include <gtest/gtest.h>

#include <ecs/Query.h>
#include <math/geometry/3d/Transform3d.h>
#include <world/transform/TransformPropagation.h>

#include <numbers>
#include <utility>
#include <vector>

struct ExtraComponent
{
    int Value = 0;
};
SENCHA_DECLARE_COMPONENT_TYPE(ExtraComponent, "test.extra_component");

TEST(TransformPropagation, RootWorldEqualsLocalInEcsWorld)
{
    World world;
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<WorldTransform>();
    world.RegisterComponent<Parent>();

    EntityId entity = world.CreateEntity();
    const Transform3f local(Vec3d(3.0f, 4.0f, 5.0f), Quatf::Identity(), Vec3d::One());
    world.AddComponent(entity, LocalTransform{ local });
    world.AddComponent(entity, WorldTransform{});

    PropagateTransforms(world);

    const WorldTransform* worldTransform = world.TryGet<WorldTransform>(entity);
    ASSERT_NE(worldTransform, nullptr);
    EXPECT_EQ(worldTransform->Value.Position, Vec3d(3.0f, 4.0f, 5.0f));
}

TEST(TransformPropagation, ChildInheritsParentInEcsWorld)
{
    World world;
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<WorldTransform>();
    world.RegisterComponent<Parent>();

    EntityId parent = world.CreateEntity();
    EntityId child = world.CreateEntity();

    const Transform3f parentLocal(Vec3d(10.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One());
    const Transform3f childLocal(Vec3d(2.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One());

    world.AddComponent(parent, LocalTransform{ parentLocal });
    world.AddComponent(parent, WorldTransform{});
    world.AddComponent(child, LocalTransform{ childLocal });
    world.AddComponent(child, WorldTransform{});
    world.AddComponent(child, Parent{ parent });

    PropagateTransforms(world);

    const WorldTransform* childWorld = world.TryGet<WorldTransform>(child);
    ASSERT_NE(childWorld, nullptr);
    EXPECT_EQ(childWorld->Value.Position, Vec3d(12.0f, 0.0f, 0.0f));
}

TEST(TransformPropagation, EcsPropagationBumpsWorldTransformChangedChunks)
{
    World world;
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<WorldTransform>();
    world.RegisterComponent<Parent>();

    EntityId entity = world.CreateEntity();
    world.AddComponent(entity, LocalTransform{ Transform3f(Vec3d(1.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(entity, WorldTransform{});

    world.AdvanceFrame();
    PropagateTransforms(world);

    uint32_t changedRows = 0;
    Query<Read<WorldTransform>, Changed<WorldTransform>> changed(world);
    changed.ForEachChunk([&](auto& view)
    {
        changedRows += view.Count();
    }, 0);

    EXPECT_EQ(changedRows, 1u);
}

TEST(TransformPropagation, RebuildsCachedPointersWhenArchetypeCountGrows)
{
    World world;
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<WorldTransform>();
    world.RegisterComponent<Parent>();
    world.RegisterComponent<ExtraComponent>();

    EntityId parent = world.CreateEntity();
    EntityId child = world.CreateEntity();

    world.AddComponent(parent, LocalTransform{ Transform3f(Vec3d(5.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(parent, WorldTransform{});
    world.AddComponent(child, LocalTransform{ Transform3f(Vec3d(1.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(child, WorldTransform{});
    world.AddComponent(child, Parent{ parent });

    PropagateTransforms(world);

    world.AddComponent(child, ExtraComponent{ 7 });
    world.TryGet<LocalTransform>(child)->Value.Position = Vec3d(3.0f, 0.0f, 0.0f);

    PropagateTransforms(world);

    const WorldTransform* childWorld = world.TryGet<WorldTransform>(child);
    ASSERT_NE(childWorld, nullptr);
    EXPECT_EQ(childWorld->Value.Position, Vec3d(8.0f, 0.0f, 0.0f));
}

TEST(TransformPropagation, ThreeLevelHierarchyInEcsWorld)
{
    World world;
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<WorldTransform>();
    world.RegisterComponent<Parent>();

    EntityId root = world.CreateEntity();
    EntityId mid  = world.CreateEntity();
    EntityId leaf = world.CreateEntity();

    world.AddComponent(root, LocalTransform{ Transform3f(Vec3d(100.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(root, WorldTransform{});
    world.AddComponent(mid, LocalTransform{ Transform3f(Vec3d(50.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(mid, WorldTransform{});
    world.AddComponent(mid, Parent{ root });
    world.AddComponent(leaf, LocalTransform{ Transform3f(Vec3d(10.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(leaf, WorldTransform{});
    world.AddComponent(leaf, Parent{ mid });

    PropagateTransforms(world);

    const WorldTransform* leafWorld = world.TryGet<WorldTransform>(leaf);
    ASSERT_NE(leafWorld, nullptr);
    EXPECT_EQ(leafWorld->Value.Position, Vec3d(160.0f, 0.0f, 0.0f));
}

TEST(TransformPropagation, ScaleComposesInEcsWorld)
{
    World world;
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<WorldTransform>();
    world.RegisterComponent<Parent>();

    EntityId parent = world.CreateEntity();
    EntityId child  = world.CreateEntity();

    world.AddComponent(parent, LocalTransform{ Transform3f(Vec3d::Zero(), Quatf::Identity(), Vec3d(2.0f, 2.0f, 2.0f)) });
    world.AddComponent(parent, WorldTransform{});
    world.AddComponent(child, LocalTransform{ Transform3f(Vec3d(10.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d(0.5f, 0.5f, 0.5f)) });
    world.AddComponent(child, WorldTransform{});
    world.AddComponent(child, Parent{ parent });

    PropagateTransforms(world);

    const WorldTransform* childWorld = world.TryGet<WorldTransform>(child);
    ASSERT_NE(childWorld, nullptr);
    EXPECT_NEAR(childWorld->Value.Position.X, 20.0f, 1e-5f);
    EXPECT_NEAR(childWorld->Value.Scale.X, 1.0f, 1e-5f);
    EXPECT_NEAR(childWorld->Value.Scale.Y, 1.0f, 1e-5f);
    EXPECT_NEAR(childWorld->Value.Scale.Z, 1.0f, 1e-5f);
}

TEST(TransformPropagation, RotatedParentAffectsChildPositionInEcsWorld)
{
    World world;
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<WorldTransform>();
    world.RegisterComponent<Parent>();

    EntityId parent = world.CreateEntity();
    EntityId child  = world.CreateEntity();

    const float halfPi = std::numbers::pi_v<float> / 2.0f;
    const Transform3f parentLocal(
        Vec3d::Zero(),
        Quatf::FromAxisAngle(Vec3d(0.0f, 0.0f, 1.0f), halfPi),
        Vec3d::One());
    const Transform3f childLocal(Vec3d(10.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One());

    world.AddComponent(parent, LocalTransform{ parentLocal });
    world.AddComponent(parent, WorldTransform{});
    world.AddComponent(child, LocalTransform{ childLocal });
    world.AddComponent(child, WorldTransform{});
    world.AddComponent(child, Parent{ parent });

    PropagateTransforms(world);

    const WorldTransform* childWorld = world.TryGet<WorldTransform>(child);
    ASSERT_NE(childWorld, nullptr);
    EXPECT_NEAR(childWorld->Value.Position.X, 0.0f, 1e-5f);
    EXPECT_NEAR(childWorld->Value.Position.Y, 10.0f, 1e-5f);
    EXPECT_NEAR(childWorld->Value.Position.Z, 0.0f, 1e-5f);
    EXPECT_TRUE(childWorld->Value.Rotation.NearlyEquals(parentLocal.Rotation, 1e-5f));
}

TEST(TransformPropagation, DestroyedSiblingSwapRemoveDoesNotStalePropagation)
{
    World world;
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<WorldTransform>();
    world.RegisterComponent<Parent>();

    EntityId a = world.CreateEntity();
    EntityId b = world.CreateEntity();
    EntityId c = world.CreateEntity();
    world.AddComponent(a, LocalTransform{ Transform3f(Vec3d(1.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(a, WorldTransform{});
    world.AddComponent(b, LocalTransform{ Transform3f(Vec3d(2.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(b, WorldTransform{});
    world.AddComponent(c, LocalTransform{ Transform3f(Vec3d(3.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(c, WorldTransform{});

    PropagateTransforms(world);

    world.DestroyEntity(a);
    world.AdvanceFrame();
    world.TryGet<LocalTransform>(c)->Value.Position = Vec3d(30.0f, 0.0f, 0.0f);

    PropagateTransforms(world);

    EXPECT_EQ(world.TryGet<WorldTransform>(b)->Value.Position, Vec3d(2.0f, 0.0f, 0.0f));
    EXPECT_EQ(world.TryGet<WorldTransform>(c)->Value.Position, Vec3d(30.0f, 0.0f, 0.0f));
}

TEST(TransformPropagation, MoveIntoExistingArchetypeInvalidatesCache)
{
    World world;
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<WorldTransform>();
    world.RegisterComponent<Parent>();
    world.RegisterComponent<ExtraComponent>();

    EntityId pre = world.CreateEntity();
    world.AddComponent(pre, LocalTransform{ Transform3f(Vec3d(9.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(pre, WorldTransform{});
    world.AddComponent(pre, ExtraComponent{ 1 });

    EntityId a = world.CreateEntity();
    world.AddComponent(a, LocalTransform{ Transform3f(Vec3d(5.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(a, WorldTransform{});

    PropagateTransforms(world);

    world.AddComponent(a, ExtraComponent{ 2 });
    world.AdvanceFrame();
    world.TryGet<LocalTransform>(a)->Value.Position = Vec3d(50.0f, 0.0f, 0.0f);

    PropagateTransforms(world);

    EXPECT_EQ(world.TryGet<WorldTransform>(a)->Value.Position, Vec3d(50.0f, 0.0f, 0.0f));
}

TEST(TransformPropagation, NewEntityInExistingArchetypeIsPropagated)
{
    World world;
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<WorldTransform>();
    world.RegisterComponent<Parent>();

    EntityId a = world.CreateEntity();
    world.AddComponent(a, LocalTransform{ Transform3f(Vec3d(1.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(a, WorldTransform{});

    PropagateTransforms(world);

    EntityId b = world.CreateEntity();
    world.AddComponent(b, LocalTransform{ Transform3f(Vec3d(7.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(b, WorldTransform{});

    PropagateTransforms(world);

    EXPECT_EQ(world.TryGet<WorldTransform>(b)->Value.Position, Vec3d(7.0f, 0.0f, 0.0f));
}

// A row that appears in a chunk after that chunk has already been swept has to be
// found. Adding a component is a write to the destination chunk, so it must read
// as one to change detection, or an entity spawned mid-session never receives a
// world transform.
TEST(TransformPropagation, EntitySpawnedAfterAnEarlierSweepIsPropagated)
{
    World world;
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<WorldTransform>();
    world.RegisterComponent<Parent>();

    const EntityId first = world.CreateEntity();
    world.AddComponent(first, LocalTransform{ Transform3f(Vec3d(1.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(first, WorldTransform{});

    world.AdvanceFrame();
    PropagateTransforms(world);
    ASSERT_EQ(world.TryGet<WorldTransform>(first)->Value.Position,
              Vec3d(1.0f, 0.0f, 0.0f));

    // Lands in the chunk the sweep above already visited.
    world.AdvanceFrame();
    const EntityId second = world.CreateEntity();
    world.AddComponent(second, LocalTransform{ Transform3f(Vec3d(7.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(second, WorldTransform{});

    PropagateTransforms(world);

    EXPECT_EQ(world.TryGet<WorldTransform>(second)->Value.Position,
              Vec3d(7.0f, 0.0f, 0.0f));
}

TEST(TransformPropagation, TryGetLocalMutationIsRepropagated)
{
    World world;
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<WorldTransform>();
    world.RegisterComponent<Parent>();

    EntityId a = world.CreateEntity();
    world.AddComponent(a, LocalTransform{ Transform3f(Vec3d(1.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(a, WorldTransform{});

    PropagateTransforms(world);

    world.AdvanceFrame();
    world.TryGet<LocalTransform>(a)->Value.Position = Vec3d(4.0f, 0.0f, 0.0f);

    PropagateTransforms(world);

    EXPECT_EQ(world.TryGet<WorldTransform>(a)->Value.Position, Vec3d(4.0f, 0.0f, 0.0f));
}

TEST(TransformPropagation, ReparentViaTryGetRebuildsOrder)
{
    World world;
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<WorldTransform>();
    world.RegisterComponent<Parent>();

    EntityId parentA = world.CreateEntity();
    EntityId parentB = world.CreateEntity();
    EntityId child   = world.CreateEntity();

    world.AddComponent(parentA, LocalTransform{ Transform3f(Vec3d(10.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(parentA, WorldTransform{});
    world.AddComponent(parentB, LocalTransform{ Transform3f(Vec3d(100.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(parentB, WorldTransform{});
    world.AddComponent(child, LocalTransform{ Transform3f(Vec3d(1.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(child, WorldTransform{});
    world.AddComponent(child, Parent{ parentA });

    PropagateTransforms(world);
    EXPECT_EQ(world.TryGet<WorldTransform>(child)->Value.Position, Vec3d(11.0f, 0.0f, 0.0f));

    world.AdvanceFrame();
    world.TryGet<Parent>(child)->Entity = parentB;

    PropagateTransforms(world);
    EXPECT_EQ(world.TryGet<WorldTransform>(child)->Value.Position, Vec3d(101.0f, 0.0f, 0.0f));
}

// The dirty test for unparented entities is chunk-granular, but a hierarchy edge
// does not respect chunk boundaries: a child sitting in an untouched chunk still
// has to recompute when its parent moves. ExtraComponent forces the child into
// its own archetype, so nothing about its own storage looks dirty.
TEST(TransformPropagation, ChildInACleanChunkFollowsAMovedParent)
{
    World world;
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<WorldTransform>();
    world.RegisterComponent<Parent>();
    world.RegisterComponent<ExtraComponent>();

    const EntityId parent = world.CreateEntity();
    world.AddComponent(parent, LocalTransform{ Transform3f(Vec3d(10.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(parent, WorldTransform{});

    const EntityId child = world.CreateEntity();
    world.AddComponent(child, LocalTransform{ Transform3f(Vec3d(1.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(child, WorldTransform{});
    world.AddComponent(child, Parent{ parent });
    world.AddComponent(child, ExtraComponent{ 1 });

    world.AdvanceFrame();
    PropagateTransforms(world);
    ASSERT_EQ(world.TryGet<WorldTransform>(child)->Value.Position,
              Vec3d(11.0f, 0.0f, 0.0f));

    world.AdvanceFrame();
    world.TryGet<LocalTransform>(parent)->Value.Position = Vec3d(20.0f, 0.0f, 0.0f);
    PropagateTransforms(world);

    EXPECT_EQ(world.TryGet<WorldTransform>(child)->Value.Position,
              Vec3d(21.0f, 0.0f, 0.0f));
}

// Entity slots are reused. A child whose parent died must fall back to its local
// transform, not inherit whatever entity now occupies the parent's slot: index
// equality is not identity.
TEST(TransformPropagation, RecycledParentSlotIsNotInherited)
{
    World world;
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<WorldTransform>();
    world.RegisterComponent<Parent>();

    const EntityId parent = world.CreateEntity();
    world.AddComponent(parent, LocalTransform{ Transform3f(Vec3d(100.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(parent, WorldTransform{});

    const EntityId child = world.CreateEntity();
    world.AddComponent(child, LocalTransform{ Transform3f(Vec3d(1.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(child, WorldTransform{});
    world.AddComponent(child, Parent{ parent });

    world.AdvanceFrame();
    PropagateTransforms(world);
    ASSERT_EQ(world.TryGet<WorldTransform>(child)->Value.Position,
              Vec3d(101.0f, 0.0f, 0.0f));

    world.DestroyEntity(parent);

    // Reuses the freed slot, and is itself parented so that it would land in the
    // order beside the orphan if identity were compared by index alone.
    const EntityId successor = world.CreateEntity();
    ASSERT_EQ(successor.Index, parent.Index);
    ASSERT_NE(successor.Generation, parent.Generation);
    world.AddComponent(successor, LocalTransform{ Transform3f(Vec3d(7.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(successor, WorldTransform{});
    world.AddComponent(successor, Parent{ child });

    world.AdvanceFrame();
    PropagateTransforms(world);

    EXPECT_EQ(world.TryGet<WorldTransform>(child)->Value.Position,
              Vec3d(1.0f, 0.0f, 0.0f))
        << "an orphaned child must fall back to its local transform";
    EXPECT_EQ(world.TryGet<WorldTransform>(successor)->Value.Position,
              Vec3d(8.0f, 0.0f, 0.0f));
}

// Scoped invalidation must produce the same transforms as rebuilding everything
// every sweep. Under-invalidation shows up as stale values rather than a crash,
// so the two are run over the same churn script and compared.
//
// Every sweep is compared, not just the last one: a divergence introduced by one
// step is routinely overwritten by the next, so a final-state comparison passes
// while the frames in between were wrong. Recording per sweep also names the step
// that broke.
TEST(TransformPropagation, ScopedInvalidationMatchesForcedFullRebuild)
{
    const auto run = [](bool forceFull, std::vector<std::vector<Vec3d>>& out)
    {
        World world;
        world.RegisterComponent<LocalTransform>();
        world.RegisterComponent<WorldTransform>();
        world.RegisterComponent<Parent>();
        world.RegisterComponent<ExtraComponent>();

        StoragePartitionSet active;
        active.Add(StoragePartitionId::Default());
        const StoragePartitionId zone{ 1 };
        active.Add(zone);

        std::vector<EntityId> entities;
        const auto spawn = [&](StoragePartitionId partition, float x, EntityId parent)
        {
            const EntityId entity = world.CreateEntity(partition);
            world.AddComponent(entity, LocalTransform{ Transform3f(Vec3d(x, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
            world.AddComponent(entity, WorldTransform{});
            if (parent.IsValid())
                world.AddComponent(entity, Parent{ parent });
            entities.push_back(entity);
            return entity;
        };

        // A three-level chain spanning two partitions, plus flat entities.
        const EntityId root = spawn(StoragePartitionId::Default(), 10.0f, EntityId{});
        const EntityId mid = spawn(zone, 2.0f, root);
        const EntityId leaf = spawn(zone, 0.5f, mid);
        const EntityId flat = spawn(zone, 3.0f, EntityId{});

        out.clear();
        const auto record = [&]
        {
            std::vector<Vec3d> positions;
            for (const EntityId entity : entities)
            {
                const WorldTransform* transform =
                    std::as_const(world).TryGet<WorldTransform>(entity);
                positions.push_back(transform != nullptr
                    ? transform->Value.Position
                    : Vec3d::Zero());
            }
            out.push_back(std::move(positions));
        };

        const auto sweep = [&]
        {
            world.AdvanceFrame();
            PropagateTransforms(
                world,
                active,
                TransformPropagationDomain::Simulation,
                forceFull);
            record();
        };

        sweep();

        // Move the root: dirtiness has to reach the leaf.
        world.TryGet<LocalTransform>(root)->Value.Position = Vec3d(11.0f, 0.0f, 0.0f);
        sweep();

        // Spawn into the streamed partition, which changes nothing structural
        // about the existing hierarchy.
        const EntityId added = spawn(zone, 4.0f, EntityId{});
        sweep();

        // Re-parent across the partition boundary.
        world.AdvanceFrame();
        world.TryGet<Parent>(leaf)->Entity = root;
        sweep();

        // Grow an archetype under a parented entity, relocating rows.
        world.AddComponent(mid, ExtraComponent{ 1 });
        sweep();

        // Drop the streamed partition out of the domain, move something inside
        // it while it sleeps, then bring it back.
        StoragePartitionSet defaultOnly;
        defaultOnly.Add(StoragePartitionId::Default());
        world.AdvanceFrame();
        PropagateTransforms(
            world,
            defaultOnly,
            TransformPropagationDomain::Simulation,
            forceFull);
        record();
        world.AdvanceFrame();
        world.TryGet<LocalTransform>(flat)->Value.Position = Vec3d(9.0f, 0.0f, 0.0f);
        sweep();

        // Destroy a parent and leave the child orphaned.
        world.DestroyEntity(mid);
        sweep();

        (void)added;
    };

    std::vector<std::vector<Vec3d>> scoped;
    std::vector<std::vector<Vec3d>> forced;
    run(false, scoped);
    run(true, forced);

    ASSERT_EQ(scoped.size(), forced.size());
    for (size_t sweep = 0; sweep < scoped.size(); ++sweep)
    {
        ASSERT_EQ(scoped[sweep].size(), forced[sweep].size());
        for (size_t index = 0; index < scoped[sweep].size(); ++index)
        {
            EXPECT_EQ(scoped[sweep][index], forced[sweep][index])
                << "sweep " << sweep << ", entity " << index
                << " diverged from the forced-rebuild result";
        }
    }
}

TEST(TransformPropagation, ChangedWorldTransformSkipsCleanChunks)
{
    World world;
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<WorldTransform>();
    world.RegisterComponent<Parent>();
    world.RegisterComponent<ExtraComponent>();

    EntityId a = world.CreateEntity();
    world.AddComponent(a, LocalTransform{ Transform3f(Vec3d(1.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(a, WorldTransform{});

    EntityId b = world.CreateEntity();
    world.AddComponent(b, LocalTransform{ Transform3f(Vec3d(2.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(b, WorldTransform{});
    world.AddComponent(b, ExtraComponent{ 1 });

    world.AdvanceFrame();
    PropagateTransforms(world);

    world.AdvanceFrame();
    world.TryGet<LocalTransform>(a)->Value.Position = Vec3d(3.0f, 0.0f, 0.0f);
    PropagateTransforms(world);

    uint32_t changedRows = 0;
    Query<Read<WorldTransform>, Changed<WorldTransform>> changed(world);
    changed.ForEachChunk([&](auto& view)
    {
        changedRows += view.Count();
    }, 1);

    EXPECT_EQ(changedRows, 1u);
    EXPECT_EQ(std::as_const(world).TryGet<WorldTransform>(a)->Value.Position,
              Vec3d(3.0f, 0.0f, 0.0f));
    EXPECT_EQ(std::as_const(world).TryGet<WorldTransform>(b)->Value.Position,
              Vec3d(2.0f, 0.0f, 0.0f));
}
