#include <gtest/gtest.h>

#include <math/geometry/3d/Transform3d.h>
#include <world/transform/TransformPropagation.h>

#include <numbers>
#include <utility>

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
