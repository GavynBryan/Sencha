#include <gtest/gtest.h>
#include <core/batch/SparseSet.h>
#include <entity/EntityId.h>
#include <math/geometry/2d/Transform2d.h>
#include <math/geometry/3d/Transform3d.h>
#include <transform/TransformHierarchyService.h>
#include <transform/TransformPropagationOrderService.h>
#include <transform/TransformPropagationSystem.h>
#include <transform/TransformSpace.h>
#include <transform/TransformStore.h>
#include <cmath>
#include <numbers>

namespace
{
    EntityId Entity(EntityIndex index, uint16_t generation = 1)
    {
        return EntityId{ index, generation };
    }

    struct TransformPropagationFixture
    {
        TransformSpace2d Space;
        TransformPropagationSystem<Transform2f> Propagation;

        TransformPropagationFixture()
            : Propagation(Space.Transforms, Space.Hierarchy, Space.PropagationOrder)
        {
        }
    };

    struct TransformPropagation3Fixture
    {
        TransformSpace3d Space;
        TransformPropagationSystem<Transform3f> Propagation;

        TransformPropagation3Fixture()
            : Propagation(Space.Transforms, Space.Hierarchy, Space.PropagationOrder)
        {
        }
    };
}

// ============================================================================
// SparseSet — transform component storage primitive
// ============================================================================

TEST(TransformSparseSet, InsertAndLookupByOwnerId)
{
    SparseSet<Transform2f> transforms;

    transforms.Insert(
        Transform2f({ 3.0f, 4.0f }, 0.0f, { 1.0f, 1.0f }),
        42);

    const Transform2f* transform = transforms.TryGet(42);
    ASSERT_NE(transform, nullptr);
    EXPECT_NEAR(transform->Position.X, 3.0f, 1e-5f);
    EXPECT_NEAR(transform->Position.Y, 4.0f, 1e-5f);
    EXPECT_EQ(transforms.Count(), 1u);
    EXPECT_TRUE(transforms.Contains(42));
    EXPECT_FALSE(transforms.Contains(41));
}

TEST(TransformSparseSet, UpsertUpdatesExistingOwnerInPlace)
{
    SparseSet<Transform2f> transforms;

    transforms.Insert(
        Transform2f({ 1.0f, 2.0f }, 0.0f, { 1.0f, 1.0f }),
        7);
    const Id firstIndex = transforms.IndexOf(7);

    transforms.Insert(
        Transform2f({ 5.0f, 6.0f }, 0.0f, { 2.0f, 2.0f }),
        7);

    const Transform2f* transform = transforms.TryGet(7);
    ASSERT_NE(transform, nullptr);
    EXPECT_EQ(transforms.Count(), 1u);
    EXPECT_EQ(transforms.IndexOf(7), firstIndex);
    EXPECT_NEAR(transform->Position.X, 5.0f, 1e-5f);
    EXPECT_NEAR(transform->Position.Y, 6.0f, 1e-5f);
    EXPECT_NEAR(transform->Scale.X, 2.0f, 1e-5f);
}

TEST(TransformSparseSet, RemoveSwapUpdatesMovedOwnerIndex)
{
    SparseSet<Transform2f> transforms;

    transforms.Insert(
        Transform2f({ 1.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }),
        10);
    transforms.Insert(
        Transform2f({ 2.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }),
        20);
    transforms.Insert(
        Transform2f({ 3.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }),
        30);

    EXPECT_TRUE(transforms.Remove(20));

    EXPECT_EQ(transforms.Count(), 2u);
    EXPECT_EQ(transforms.TryGet(20), nullptr);
    EXPECT_TRUE(transforms.Contains(10));
    EXPECT_TRUE(transforms.Contains(30));

    const Transform2f* moved = transforms.TryGet(30);
    ASSERT_NE(moved, nullptr);
    EXPECT_NEAR(moved->Position.X, 3.0f, 1e-5f);
    EXPECT_NE(transforms.IndexOf(30), InvalidId);

    const auto& owners = transforms.GetOwners();
    ASSERT_EQ(owners.size(), transforms.GetItems().size());
    for (size_t i = 0; i < owners.size(); ++i)
        EXPECT_EQ(transforms.IndexOf(owners[i]), static_cast<Id>(i));
}

TEST(TransformSparseSet, RemoveMissingOwnerIsNoOp)
{
    SparseSet<Transform2f> transforms;
    transforms.Insert(Transform2f::Identity(), 1);

    const uint64_t version = transforms.GetVersion();
    EXPECT_FALSE(transforms.Remove(2));
    EXPECT_EQ(transforms.Count(), 1u);
    EXPECT_EQ(transforms.GetVersion(), version);
}

TEST(TransformSparseSet, ClearInvalidatesOwners)
{
    SparseSet<Transform2f> transforms;
    transforms.Insert(Transform2f::Identity(), 3);
    transforms.Insert(Transform2f::Identity(), 9);

    transforms.Clear();

    EXPECT_TRUE(transforms.IsEmpty());
    EXPECT_EQ(transforms.TryGet(3), nullptr);
    EXPECT_EQ(transforms.TryGet(9), nullptr);
    EXPECT_TRUE(transforms.GetOwners().empty());
}

// ============================================================================
// TransformHierarchyService
// ============================================================================

TEST(TransformHierarchy, RegisterAndQueryRoots)
{
    TransformHierarchyService hierarchy;

    EntityId a = Entity(1);
    EntityId b = Entity(2);

    hierarchy.Register(a);
    hierarchy.Register(b);

    auto roots = hierarchy.GetRoots();
    EXPECT_EQ(roots.size(), 2u);
    EXPECT_TRUE(hierarchy.IsRegistered(a));
    EXPECT_TRUE(hierarchy.IsRegistered(b));
}

TEST(TransformHierarchy, SetParentCreatesRelationship)
{
    TransformHierarchyService hierarchy;

    EntityId parent = Entity(1);
    EntityId child = Entity(2);

    hierarchy.SetParent(child, parent);

    EXPECT_TRUE(hierarchy.HasParent(child));
    EXPECT_EQ(hierarchy.GetParent(child), parent);
    EXPECT_TRUE(hierarchy.HasChildren(parent));

    const auto& children = hierarchy.GetChildren(parent);
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children[0], child);
}

TEST(TransformHierarchy, ClearParentOrphansChild)
{
    TransformHierarchyService hierarchy;

    EntityId parent = Entity(1);
    EntityId child = Entity(2);

    hierarchy.SetParent(child, parent);
    hierarchy.ClearParent(child);

    EXPECT_FALSE(hierarchy.HasParent(child));
    EXPECT_FALSE(hierarchy.HasChildren(parent));
    EXPECT_EQ(hierarchy.GetRoots().size(), 2u);
}

TEST(TransformHierarchy, ReparentMovesChild)
{
    TransformHierarchyService hierarchy;

    EntityId parentA = Entity(1);
    EntityId parentB = Entity(2);
    EntityId child = Entity(3);

    hierarchy.SetParent(child, parentA);
    hierarchy.SetParent(child, parentB);

    EXPECT_EQ(hierarchy.GetParent(child), parentB);
    EXPECT_FALSE(hierarchy.HasChildren(parentA));
    EXPECT_TRUE(hierarchy.HasChildren(parentB));
}

TEST(TransformHierarchy, UnregisterOrphansChildren)
{
    TransformHierarchyService hierarchy;

    EntityId parent = Entity(1);
    EntityId childA = Entity(2);
    EntityId childB = Entity(3);

    hierarchy.SetParent(childA, parent);
    hierarchy.SetParent(childB, parent);
    hierarchy.Unregister(parent);

    EXPECT_FALSE(hierarchy.IsRegistered(parent));
    EXPECT_FALSE(hierarchy.HasParent(childA));
    EXPECT_FALSE(hierarchy.HasParent(childB));
    EXPECT_TRUE(hierarchy.IsRegistered(childA));
    EXPECT_TRUE(hierarchy.IsRegistered(childB));
}

// ============================================================================
// TransformStore
// ============================================================================

TEST(TransformStore2D, AddProvisionsLocalAndWorldForEntity)
{
    TransformPropagationFixture fixture;
    EntityId entity = Entity(10);

    EXPECT_TRUE(fixture.Space.Transforms.Add(
        entity,
        Transform2f({ 3.0f, 4.0f }, 0.5f, { 2.0f, 2.0f })));

    const Transform2f* local = fixture.Space.Transforms.TryGetLocal(entity);
    const Transform2f* world = fixture.Space.Transforms.TryGetWorld(entity);

    ASSERT_NE(local, nullptr);
    ASSERT_NE(world, nullptr);
    EXPECT_NEAR(local->Position.X, 3.0f, 1e-5f);
    EXPECT_NEAR(local->Position.Y, 4.0f, 1e-5f);
    EXPECT_TRUE(world->NearlyEquals(Transform2f::Identity()));
}

TEST(TransformStore2D, RemoveFreesEntityTransform)
{
    TransformPropagationFixture fixture;
    EntityId entity = Entity(10);

    fixture.Space.Transforms.Add(entity, Transform2f::Identity());
    EXPECT_TRUE(fixture.Space.Transforms.Remove(entity));

    EXPECT_EQ(fixture.Space.Transforms.TryGetLocal(entity), nullptr);
    EXPECT_EQ(fixture.Space.Transforms.TryGetWorld(entity), nullptr);
    EXPECT_FALSE(fixture.Space.Transforms.Remove(entity));
}

TEST(TransformStore2D, MutableLocalMarksTransformDirty)
{
    TransformPropagationFixture fixture;
    EntityId entity = Entity(10);

    fixture.Space.Transforms.Add(entity, Transform2f({ 1.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }));
    fixture.Space.Hierarchy.Register(entity);
    fixture.Propagation.Propagate();

    Transform2f* local = fixture.Space.Transforms.TryGetLocalMutable(entity);
    ASSERT_NE(local, nullptr);
    local->Position.X = 5.0f;

    fixture.Propagation.Propagate();

    const Transform2f* world = fixture.Space.Transforms.TryGetWorld(entity);
    ASSERT_NE(world, nullptr);
    EXPECT_NEAR(world->Position.X, 5.0f, 1e-5f);
}

// ============================================================================
// TransformPropagationSystem
// ============================================================================

TEST(TransformPropagation, RootWorldEqualsLocal2D)
{
    TransformPropagationFixture fixture;
    EntityId entity = Entity(1);

    Transform2f local({ 10.0f, 20.0f }, 0.0f, { 1.0f, 1.0f });
    fixture.Space.Transforms.Add(entity, local);
    fixture.Space.Hierarchy.Register(entity);

    fixture.Propagation.Propagate();

    const Transform2f* world = fixture.Space.Transforms.TryGetWorld(entity);
    ASSERT_NE(world, nullptr);
    EXPECT_TRUE(world->NearlyEquals(local));
}

TEST(TransformPropagation, ChildInheritsParentTransform2D)
{
    TransformPropagationFixture fixture;
    EntityId parent = Entity(1);
    EntityId child = Entity(2);

    fixture.Space.Transforms.Add(parent, Transform2f({ 100.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }));
    fixture.Space.Transforms.Add(child, Transform2f({ 10.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }));
    fixture.Space.Hierarchy.SetParent(child, parent);

    fixture.Propagation.Propagate();

    const Transform2f* parentWorld = fixture.Space.Transforms.TryGetWorld(parent);
    const Transform2f* childWorld = fixture.Space.Transforms.TryGetWorld(child);
    ASSERT_NE(parentWorld, nullptr);
    ASSERT_NE(childWorld, nullptr);

    EXPECT_NEAR(parentWorld->Position.X, 100.0f, 1e-5f);
    EXPECT_NEAR(childWorld->Position.X, 110.0f, 1e-5f);
}

TEST(TransformPropagation, RotatedParentAffectsChildPosition2D)
{
    TransformPropagationFixture fixture;
    EntityId parent = Entity(1);
    EntityId child = Entity(2);

    float halfPi = std::numbers::pi_v<float> / 2.0f;
    fixture.Space.Transforms.Add(parent, Transform2f({ 0.0f, 0.0f }, halfPi, { 1.0f, 1.0f }));
    fixture.Space.Transforms.Add(child, Transform2f({ 10.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }));
    fixture.Space.Hierarchy.SetParent(child, parent);

    fixture.Propagation.Propagate();

    const Transform2f* childWorld = fixture.Space.Transforms.TryGetWorld(child);
    ASSERT_NE(childWorld, nullptr);

    EXPECT_NEAR(childWorld->Position.X, 0.0f, 1e-5f);
    EXPECT_NEAR(childWorld->Position.Y, 10.0f, 1e-5f);
    EXPECT_NEAR(childWorld->Rotation, halfPi, 1e-5f);
}

TEST(TransformPropagation, ThreeLevelHierarchy2D)
{
    TransformPropagationFixture fixture;
    EntityId root = Entity(1);
    EntityId mid = Entity(2);
    EntityId leaf = Entity(3);

    fixture.Space.Transforms.Add(root, Transform2f({ 100.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }));
    fixture.Space.Transforms.Add(mid, Transform2f({ 50.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }));
    fixture.Space.Transforms.Add(leaf, Transform2f({ 10.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }));
    fixture.Space.Hierarchy.SetParent(mid, root);
    fixture.Space.Hierarchy.SetParent(leaf, mid);

    fixture.Propagation.Propagate();

    const Transform2f* leafWorld = fixture.Space.Transforms.TryGetWorld(leaf);
    ASSERT_NE(leafWorld, nullptr);
    EXPECT_NEAR(leafWorld->Position.X, 160.0f, 1e-5f);
}

TEST(TransformPropagation, ScaleComposes2D)
{
    TransformPropagationFixture fixture;
    EntityId parent = Entity(1);
    EntityId child = Entity(2);

    fixture.Space.Transforms.Add(parent, Transform2f({ 0.0f, 0.0f }, 0.0f, { 2.0f, 2.0f }));
    fixture.Space.Transforms.Add(child, Transform2f({ 10.0f, 0.0f }, 0.0f, { 0.5f, 0.5f }));
    fixture.Space.Hierarchy.SetParent(child, parent);

    fixture.Propagation.Propagate();

    const Transform2f* childWorld = fixture.Space.Transforms.TryGetWorld(child);
    ASSERT_NE(childWorld, nullptr);
    EXPECT_NEAR(childWorld->Position.X, 20.0f, 1e-5f);
    EXPECT_NEAR(childWorld->Scale.X, 1.0f, 1e-5f);
    EXPECT_NEAR(childWorld->Scale.Y, 1.0f, 1e-5f);
}

TEST(TransformPropagation, RemovedTransformIsSkippedOnRebuild)
{
    TransformPropagationFixture fixture;
    EntityId parent = Entity(1);
    EntityId child = Entity(2);

    fixture.Space.Transforms.Add(parent, Transform2f({ 100.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }));
    fixture.Space.Transforms.Add(child, Transform2f({ 10.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }));
    fixture.Space.Hierarchy.SetParent(child, parent);
    fixture.Space.Transforms.Remove(child);

    fixture.Propagation.Propagate();

    EXPECT_EQ(fixture.Space.Transforms.TryGetWorld(child), nullptr);
    ASSERT_EQ(fixture.Space.PropagationOrder.GetOrder().size(), 1u);
}

TEST(TransformPropagation, ChildInheritsParentTransform3D)
{
    TransformPropagation3Fixture fixture;
    EntityId parent = Entity(1);
    EntityId child = Entity(2);

    float halfPi = std::numbers::pi_v<float> / 2.0f;
    Transform3f parentLocal(
        Vec3d::Zero(),
        Quatf::FromAxisAngle(Vec3d(0.0f, 0.0f, 1.0f), halfPi),
        Vec3d::One());
    Transform3f childLocal(
        Vec3d(10.0f, 0.0f, 0.0f),
        Quatf::Identity(),
        Vec3d::One());

    fixture.Space.Transforms.Add(parent, parentLocal);
    fixture.Space.Transforms.Add(child, childLocal);
    fixture.Space.Hierarchy.SetParent(child, parent);

    fixture.Propagation.Propagate();

    const Transform3f* childWorld = fixture.Space.Transforms.TryGetWorld(child);
    ASSERT_NE(childWorld, nullptr);

    EXPECT_NEAR(childWorld->Position.X, 0.0f, 1e-5f);
    EXPECT_NEAR(childWorld->Position.Y, 10.0f, 1e-5f);
    EXPECT_NEAR(childWorld->Position.Z, 0.0f, 1e-5f);
    EXPECT_TRUE(childWorld->Rotation.NearlyEquals(parentLocal.Rotation, 1e-5f));
}

TEST(TransformSpace2d, ResolvesGameplayFacingTransformServices)
{
    TransformSpace2d registry;
    EntityId entity = Entity(99);

    EXPECT_TRUE(registry.Transforms.Add(
        entity,
        Transform2f({ 1.0f, 2.0f }, 0.0f, { 1.0f, 1.0f })));
    registry.Hierarchy.Register(entity);

    EXPECT_TRUE(registry.Hierarchy.IsRegistered(entity));
    EXPECT_NE(registry.Transforms.TryGetLocal(entity), nullptr);
    EXPECT_NE(registry.Transforms.TryGetWorld(entity), nullptr);
}
