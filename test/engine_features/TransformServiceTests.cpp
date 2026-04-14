#include <gtest/gtest.h>
#include <core/batch/DataBatch.h>
#include <math/geometry/2d/Transform2d.h>
#include <math/geometry/3d/Transform3d.h>
#include <world/transform/TransformStore.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPropagationOrderService.h>
#include <world/transform/TransformPropagationSystem.h>
#include <world/scene/SceneNode.h>
#include <world/tilemap/Tilemap2d.h>
#include <world/World.h>
#include <core/raii/DataBatchHandle.h>
#include <core/service/ServiceHost.h>
#include <cmath>
#include <numbers>

namespace
{
	using Transform2Hierarchy = TransformHierarchyService;
	using Transform2PropagationOrder = TransformPropagationOrderService;
	using Transform2Propagation = TransformPropagationSystem<Transform2f>;
	using Transform3Hierarchy = TransformHierarchyService;
	using Transform3PropagationOrder = TransformPropagationOrderService;
	using Transform3Propagation = TransformPropagationSystem<Transform3f>;
}

// ============================================================================
// DataBatch key-based access
// ============================================================================

struct DummyValue
{
	int V = 0;
	DummyValue() = default;
	DummyValue(int v) : V(v) {}
};

TEST(DataBatch, TryGetByRawKey)
{
	DataBatch<DummyValue> batch;
	auto handle = batch.Emplace(42);

	DataBatchKey key = handle.GetToken();
	auto* val = batch.TryGet(key);
	ASSERT_NE(val, nullptr);
	EXPECT_EQ(val->V, 42);
}

TEST(DataBatch, TryGetByRawKeyConst)
{
	DataBatch<DummyValue> batch;
	auto handle = batch.Emplace(7);

	DataBatchKey key = handle.GetToken();
	const auto& constBatch = batch;
	const auto* val = constBatch.TryGet(key);
	ASSERT_NE(val, nullptr);
	EXPECT_EQ(val->V, 7);
}

TEST(DataBatch, TryGetByInvalidKeyReturnsNull)
{
	DataBatch<DummyValue> batch;
	batch.Emplace(1);

	DataBatchKey bogus{ 9999 };
	EXPECT_EQ(batch.TryGet(bogus), nullptr);

	DataBatchKey null{};
	EXPECT_EQ(batch.TryGet(null), nullptr);
}

TEST(DataBatch, ContainsKey)
{
	DataBatch<DummyValue> batch;
	auto handle = batch.Emplace(1);
	DataBatchKey key = handle.GetToken();

	EXPECT_TRUE(batch.Contains(key));
	EXPECT_FALSE(batch.Contains(DataBatchKey{ 9999 }));
	EXPECT_FALSE(batch.Contains(DataBatchKey{}));
}

TEST(DataBatch, ContainsAfterRemoval)
{
	DataBatch<DummyValue> batch;
	auto handle = batch.Emplace(1);
	DataBatchKey key = handle.GetToken();

	EXPECT_TRUE(batch.Contains(key));
	handle.Reset();
	EXPECT_FALSE(batch.Contains(key));
}

TEST(DataBatch, ReserveDoesNotChangeCount)
{
	DataBatch<DummyValue> batch;
	batch.Reserve(100);
	EXPECT_EQ(batch.Count(), 0u);
}

// ============================================================================
// TransformHierarchyService
// ============================================================================

TEST(TransformHierarchy, RegisterAndQueryRoots)
{
	Transform2Hierarchy hierarchy;

	DataBatchKey a{ 1 };
	DataBatchKey b{ 2 };

	hierarchy.Register(a);
	hierarchy.Register(b);

	auto roots = hierarchy.GetRoots();
	EXPECT_EQ(roots.size(), 2u);
	EXPECT_TRUE(hierarchy.IsRegistered(a));
	EXPECT_TRUE(hierarchy.IsRegistered(b));
}

TEST(TransformHierarchy, SetParentCreatesRelationship)
{
	Transform2Hierarchy hierarchy;

	DataBatchKey parent{ 1 };
	DataBatchKey child{ 2 };

	hierarchy.Register(parent);
	hierarchy.Register(child);
	hierarchy.SetParent(child, parent);

	EXPECT_TRUE(hierarchy.HasParent(child));
	EXPECT_EQ(hierarchy.GetParent(child).Value, parent.Value);
	EXPECT_TRUE(hierarchy.HasChildren(parent));

	const auto& children = hierarchy.GetChildren(parent);
	ASSERT_EQ(children.size(), 1u);
	EXPECT_EQ(children[0], child.Value);
}

TEST(TransformHierarchy, SetParentAutoRegisters)
{
	Transform2Hierarchy hierarchy;

	DataBatchKey parent{ 1 };
	DataBatchKey child{ 2 };

	// SetParent should register the child and parent implicitly
	hierarchy.Register(child);
	hierarchy.SetParent(child, parent);

	EXPECT_TRUE(hierarchy.IsRegistered(parent));
	EXPECT_TRUE(hierarchy.IsRegistered(child));
}

TEST(TransformHierarchy, ClearParentOrphansChild)
{
	Transform2Hierarchy hierarchy;

	DataBatchKey parent{ 1 };
	DataBatchKey child{ 2 };

	hierarchy.Register(parent);
	hierarchy.Register(child);
	hierarchy.SetParent(child, parent);
	hierarchy.ClearParent(child);

	EXPECT_FALSE(hierarchy.HasParent(child));
	EXPECT_FALSE(hierarchy.HasChildren(parent));

	// Both should now be roots
	auto roots = hierarchy.GetRoots();
	EXPECT_EQ(roots.size(), 2u);
}

TEST(TransformHierarchy, ReparentMovesChild)
{
	Transform2Hierarchy hierarchy;

	DataBatchKey parentA{ 1 };
	DataBatchKey parentB{ 2 };
	DataBatchKey child{ 3 };

	hierarchy.Register(parentA);
	hierarchy.Register(parentB);
	hierarchy.Register(child);

	hierarchy.SetParent(child, parentA);
	EXPECT_EQ(hierarchy.GetParent(child).Value, parentA.Value);

	hierarchy.SetParent(child, parentB);
	EXPECT_EQ(hierarchy.GetParent(child).Value, parentB.Value);
	EXPECT_FALSE(hierarchy.HasChildren(parentA));
	EXPECT_TRUE(hierarchy.HasChildren(parentB));
}

TEST(TransformHierarchy, UnregisterOrphansChildren)
{
	Transform2Hierarchy hierarchy;

	DataBatchKey parent{ 1 };
	DataBatchKey childA{ 2 };
	DataBatchKey childB{ 3 };

	hierarchy.Register(parent);
	hierarchy.Register(childA);
	hierarchy.Register(childB);

	hierarchy.SetParent(childA, parent);
	hierarchy.SetParent(childB, parent);

	hierarchy.Unregister(parent);

	EXPECT_FALSE(hierarchy.IsRegistered(parent));
	EXPECT_FALSE(hierarchy.HasParent(childA));
	EXPECT_FALSE(hierarchy.HasParent(childB));

	// Children are now roots
	auto roots = hierarchy.GetRoots();
	EXPECT_EQ(roots.size(), 2u);
}

TEST(TransformHierarchy, RootsExcludesParentedKeys)
{
	Transform2Hierarchy hierarchy;

	DataBatchKey root{ 1 };
	DataBatchKey child{ 2 };
	DataBatchKey grandchild{ 3 };

	hierarchy.Register(root);
	hierarchy.Register(child);
	hierarchy.Register(grandchild);

	hierarchy.SetParent(child, root);
	hierarchy.SetParent(grandchild, child);

	auto roots = hierarchy.GetRoots();
	ASSERT_EQ(roots.size(), 1u);
	EXPECT_EQ(roots[0].Value, root.Value);
}

// ============================================================================
// TransformPropagationSystem
// ============================================================================

struct TransformPropagationFixture
{
	ServiceHost Host;
	World2d& GameWorld;
	DataBatch<Transform2f>& Locals;
	DataBatch<Transform2f>& Worlds;
	TransformStore2D& Store;
	Transform2Hierarchy& Hierarchy;
	Transform2PropagationOrder& PropagationOrder;
	Transform2Propagation Propagation;

	TransformPropagationFixture()
		: GameWorld(Host.AddService<World2d>())
		, Locals(GameWorld.GetLocalTransformsForEngineWiring())
		, Worlds(GameWorld.GetWorldTransformsForEngineWiring())
		, Store(GameWorld.Transforms)
		, Hierarchy(GameWorld.TransformHierarchy)
		, PropagationOrder(GameWorld.GetPropagationOrderForEngineWiring())
		, Propagation(Locals, Worlds, Hierarchy, PropagationOrder)
	{
	}
};

struct TransformPropagation3Fixture
{
	ServiceHost Host;
	World3d& GameWorld;
	DataBatch<Transform3f>& Locals;
	DataBatch<Transform3f>& Worlds;
	TransformStore3D& Store;
	Transform3Hierarchy& Hierarchy;
	Transform3PropagationOrder& PropagationOrder;
	Transform3Propagation Propagation;

	TransformPropagation3Fixture()
		: GameWorld(Host.AddService<World3d>())
		, Locals(GameWorld.GetLocalTransformsForEngineWiring())
		, Worlds(GameWorld.GetWorldTransformsForEngineWiring())
		, Store(GameWorld.Transforms)
		, Hierarchy(GameWorld.TransformHierarchy)
		, PropagationOrder(GameWorld.GetPropagationOrderForEngineWiring())
		, Propagation(Locals, Worlds, Hierarchy, PropagationOrder)
	{
	}
};

TEST(TransformPropagation, RootWorldEqualsLocal2D)
{
	TransformPropagationFixture transformServices;
	auto& transforms = transformServices.Store;
	auto& hierarchy = transformServices.Hierarchy;
	auto& propagation = transformServices.Propagation;

	Transform2f localTransform({ 10.0f, 20.0f }, 0.0f, { 1.0f, 1.0f });
	auto transformHandle = transforms.Emplace(localTransform);

	DataBatchKey key = transformHandle.GetToken();

	hierarchy.Register(key);
	propagation.Propagate();

	const auto* world = transforms.TryGetWorld(key);
	ASSERT_NE(world, nullptr);
	EXPECT_TRUE(world->NearlyEquals(localTransform));
}

TEST(TransformStore2D, EmplaceProvisionsLocalAndWorldAtSharedKey)
{
	TransformPropagationFixture transformServices;
	auto& transforms = transformServices.Store;

	auto handle = transforms.Emplace(
		Transform2f({ 3.0f, 4.0f }, 0.5f, { 2.0f, 2.0f }));
	const DataBatchKey key = handle.GetToken();

	const Transform2f* local = transforms.TryGetLocal(key);
	const Transform2f* world = transforms.TryGetWorld(key);

	ASSERT_NE(local, nullptr);
	ASSERT_NE(world, nullptr);
	EXPECT_TRUE(world->NearlyEquals(Transform2f::Identity()));

	handle.Reset();

	EXPECT_EQ(transforms.TryGetLocal(key), nullptr);
	EXPECT_EQ(transforms.TryGetWorld(key), nullptr);
}

TEST(World2d, ResolvesGameplayFacingTransformServices)
{
	TransformPropagationFixture transformServices;
	World2d& world = transformServices.GameWorld;

	auto handle = world.Transforms.Emplace(
		Transform2f({ 1.0f, 2.0f }, 0.0f, { 1.0f, 1.0f }));
	const DataBatchKey key = handle.GetToken();

	world.TransformHierarchy.Register(key);

	EXPECT_TRUE(world.TransformHierarchy.IsRegistered(key));
	EXPECT_NE(world.Transforms.TryGetLocal(key), nullptr);
	EXPECT_NE(world.Transforms.TryGetWorld(key), nullptr);
}

TEST(TransformPropagation, OrderCacheSurvivesSystemRecreation)
{
	DataBatch<Transform2f> locals;
	DataBatch<Transform2f> worlds;
	Transform2Hierarchy hierarchy;
	Transform2PropagationOrder cache;

	Transform2f parentLocal({ 5.0f, 0.0f }, 0.0f, { 1.0f, 1.0f });
	auto parentLocalH = locals.Emplace(parentLocal);
	auto parentWorldH = worlds.Emplace(Transform2f::Identity());
	DataBatchKey parentKey = parentLocalH.GetToken();

	Transform2f childLocal({ 3.0f, 0.0f }, 0.0f, { 1.0f, 1.0f });
	auto childLocalH = locals.Emplace(childLocal);
	auto childWorldH = worlds.Emplace(Transform2f::Identity());
	DataBatchKey childKey = childLocalH.GetToken();

	hierarchy.Register(parentKey);
	hierarchy.Register(childKey);
	hierarchy.SetParent(childKey, parentKey);

	{
		Transform2Propagation propagation(locals, worlds, hierarchy, cache);
		propagation.Propagate();
	}

	auto cachedOrder = cache.GetOrder();
	ASSERT_EQ(cachedOrder.size(), 2u);
	const auto* cachedOrderStorage = cachedOrder.data();

	{
		Transform2Propagation recreatedPropagation(locals, worlds, hierarchy, cache);
		recreatedPropagation.Propagate();
	}

	auto reusedOrder = cache.GetOrder();
	EXPECT_EQ(reusedOrder.size(), 2u);
	EXPECT_EQ(reusedOrder.data(), cachedOrderStorage);

	const auto* childWorld = worlds.TryGet(childKey);
	ASSERT_NE(childWorld, nullptr);
	Transform2f expectedChildWorld({ 8.0f, 0.0f }, 0.0f, { 1.0f, 1.0f });
	EXPECT_TRUE(childWorld->NearlyEquals(expectedChildWorld));
}

TEST(TransformPropagation, ChildInheritsParentTransform2D)
{
	TransformPropagationFixture transformServices;
	auto& locals = transformServices.Locals;
	auto& worlds = transformServices.Worlds;
	auto& hierarchy = transformServices.Hierarchy;
	auto& propagation = transformServices.Propagation;

	// Parent at (100, 0), no rotation
	Transform2f parentLocal({ 100.0f, 0.0f }, 0.0f, { 1.0f, 1.0f });
	auto parentLocalH = locals.Emplace(parentLocal);
	auto parentWorldH = worlds.Emplace(Transform2f::Identity());
	DataBatchKey parentKey = parentLocalH.GetToken();

	// Child at (10, 0) in parent space
	Transform2f childLocal({ 10.0f, 0.0f }, 0.0f, { 1.0f, 1.0f });
	auto childLocalH = locals.Emplace(childLocal);
	auto childWorldH = worlds.Emplace(Transform2f::Identity());
	DataBatchKey childKey = childLocalH.GetToken();

	hierarchy.Register(parentKey);
	hierarchy.Register(childKey);
	hierarchy.SetParent(childKey, parentKey);

	propagation.Propagate();

	const auto* parentWorld = worlds.TryGet(parentKey);
	const auto* childWorld = worlds.TryGet(childKey);
	ASSERT_NE(parentWorld, nullptr);
	ASSERT_NE(childWorld, nullptr);

	// Parent world = (100, 0)
	EXPECT_TRUE(parentWorld->NearlyEquals(parentLocal));

	// Child world = parent * child = (110, 0)
	Transform2f expectedChild({ 110.0f, 0.0f }, 0.0f, { 1.0f, 1.0f });
	EXPECT_TRUE(childWorld->NearlyEquals(expectedChild));
}

TEST(TransformPropagation, RotatedParentAffectsChildPosition2D)
{
	TransformPropagationFixture transformServices;
	auto& locals = transformServices.Locals;
	auto& worlds = transformServices.Worlds;
	auto& hierarchy = transformServices.Hierarchy;
	auto& propagation = transformServices.Propagation;

	// Parent at origin, rotated 90 degrees (pi/2)
	float halfPi = std::numbers::pi_v<float> / 2.0f;
	Transform2f parentLocal({ 0.0f, 0.0f }, halfPi, { 1.0f, 1.0f });
	auto parentLocalH = locals.Emplace(parentLocal);
	auto parentWorldH = worlds.Emplace(Transform2f::Identity());
	DataBatchKey parentKey = parentLocalH.GetToken();

	// Child at (10, 0) in parent space â€” should end up at (0, 10) in world
	Transform2f childLocal({ 10.0f, 0.0f }, 0.0f, { 1.0f, 1.0f });
	auto childLocalH = locals.Emplace(childLocal);
	auto childWorldH = worlds.Emplace(Transform2f::Identity());
	DataBatchKey childKey = childLocalH.GetToken();

	hierarchy.Register(parentKey);
	hierarchy.Register(childKey);
	hierarchy.SetParent(childKey, parentKey);

	propagation.Propagate();

	const auto* childWorld = worlds.TryGet(childKey);
	ASSERT_NE(childWorld, nullptr);

	// After 90-degree rotation, (10, 0) becomes approximately (-0, 10)
	// due to cos(pi/2)~0, sin(pi/2)~1
	EXPECT_NEAR(childWorld->Position.X, 0.0f, 1e-5f);
	EXPECT_NEAR(childWorld->Position.Y, 10.0f, 1e-5f);
	EXPECT_NEAR(childWorld->Rotation, halfPi, 1e-5f);
}

TEST(TransformPropagation, ThreeLevelHierarchy2D)
{
	TransformPropagationFixture transformServices;
	auto& locals = transformServices.Locals;
	auto& worlds = transformServices.Worlds;
	auto& hierarchy = transformServices.Hierarchy;
	auto& propagation = transformServices.Propagation;

	// Root at (100, 0)
	auto rootLocalH = locals.Emplace(Transform2f({ 100.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }));
	auto rootWorldH = worlds.Emplace(Transform2f::Identity());
	DataBatchKey rootKey = rootLocalH.GetToken();

	// Mid at (50, 0) in root space
	auto midLocalH = locals.Emplace(Transform2f({ 50.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }));
	auto midWorldH = worlds.Emplace(Transform2f::Identity());
	DataBatchKey midKey = midLocalH.GetToken();

	// Leaf at (10, 0) in mid space
	auto leafLocalH = locals.Emplace(Transform2f({ 10.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }));
	auto leafWorldH = worlds.Emplace(Transform2f::Identity());
	DataBatchKey leafKey = leafLocalH.GetToken();

	hierarchy.Register(rootKey);
	hierarchy.Register(midKey);
	hierarchy.Register(leafKey);
	hierarchy.SetParent(midKey, rootKey);
	hierarchy.SetParent(leafKey, midKey);

	propagation.Propagate();

	const auto* leafWorld = worlds.TryGet(leafKey);
	ASSERT_NE(leafWorld, nullptr);

	// 100 + 50 + 10 = 160
	Transform2f expected({ 160.0f, 0.0f }, 0.0f, { 1.0f, 1.0f });
	EXPECT_TRUE(leafWorld->NearlyEquals(expected));
}

TEST(TransformPropagation, ScaleComposes2D)
{
	TransformPropagationFixture transformServices;
	auto& locals = transformServices.Locals;
	auto& worlds = transformServices.Worlds;
	auto& hierarchy = transformServices.Hierarchy;
	auto& propagation = transformServices.Propagation;

	// Parent with 2x scale
	auto parentLocalH = locals.Emplace(Transform2f({ 0.0f, 0.0f }, 0.0f, { 2.0f, 2.0f }));
	auto parentWorldH = worlds.Emplace(Transform2f::Identity());
	DataBatchKey parentKey = parentLocalH.GetToken();

	// Child at (10, 0) with 0.5x scale
	auto childLocalH = locals.Emplace(Transform2f({ 10.0f, 0.0f }, 0.0f, { 0.5f, 0.5f }));
	auto childWorldH = worlds.Emplace(Transform2f::Identity());
	DataBatchKey childKey = childLocalH.GetToken();

	hierarchy.Register(parentKey);
	hierarchy.Register(childKey);
	hierarchy.SetParent(childKey, parentKey);

	propagation.Propagate();

	const auto* childWorld = worlds.TryGet(childKey);
	ASSERT_NE(childWorld, nullptr);

	// Child position in world: parent transforms (10, 0) by scale 2 â†’ (20, 0)
	EXPECT_NEAR(childWorld->Position.X, 20.0f, 1e-5f);
	EXPECT_NEAR(childWorld->Position.Y, 0.0f, 1e-5f);
	// Scale composes: 2 * 0.5 = 1
	EXPECT_NEAR(childWorld->Scale.X, 1.0f, 1e-5f);
	EXPECT_NEAR(childWorld->Scale.Y, 1.0f, 1e-5f);
}

TEST(TransformPropagation, ChildInheritsParentTransform3D)
{
	TransformPropagation3Fixture transformServices;
	auto& locals = transformServices.Locals;
	auto& worlds = transformServices.Worlds;
	auto& hierarchy = transformServices.Hierarchy;
	auto& propagation = transformServices.Propagation;

	float halfPi = std::numbers::pi_v<float> / 2.0f;
	Transform3f parentLocal(
		Vec3d::Zero(),
		Quatf::FromAxisAngle(Vec3d(0.0f, 0.0f, 1.0f), halfPi),
		Vec3d::One());
	auto parentLocalH = locals.Emplace(parentLocal);
	auto parentWorldH = worlds.Emplace(Transform3f::Identity());
	DataBatchKey parentKey = parentLocalH.GetToken();

	Transform3f childLocal(
		Vec3d(10.0f, 0.0f, 0.0f),
		Quatf::Identity(),
		Vec3d::One());
	auto childLocalH = locals.Emplace(childLocal);
	auto childWorldH = worlds.Emplace(Transform3f::Identity());
	DataBatchKey childKey = childLocalH.GetToken();

	hierarchy.Register(parentKey);
	hierarchy.Register(childKey);
	hierarchy.SetParent(childKey, parentKey);

	propagation.Propagate();

	const auto* childWorld = worlds.TryGet(childKey);
	ASSERT_NE(childWorld, nullptr);

	EXPECT_NEAR(childWorld->Position.X, 0.0f, 1e-5f);
	EXPECT_NEAR(childWorld->Position.Y, 10.0f, 1e-5f);
	EXPECT_NEAR(childWorld->Position.Z, 0.0f, 1e-5f);
	EXPECT_TRUE(childWorld->Rotation.NearlyEquals(parentLocal.Rotation, 1e-5f));
}

// ============================================================================
// Tilemap2d — proof of non-SceneNode participation in the transform system
// ============================================================================

TEST(Tilemap2d, ParticipatesInHierarchyAlongsideSceneNode)
{
	TransformPropagationFixture transformServices;
	auto& world = transformServices.GameWorld;
	auto& propagation = transformServices.Propagation;

	SceneNode2D levelRoot(world, Transform2f({ 0.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }));
	SceneNode2D player(world, Transform2f({ 50.0f, 30.0f }, 0.0f, { 1.0f, 1.0f }));
	player.SetParent(levelRoot);

	Tilemap2d tilemap(world,
		Transform2f({ -100.0f, -100.0f }, 0.0f, { 1.0f, 1.0f }),
		64, 64, 16.0f);
	tilemap.SetTransformParent(levelRoot.TransformKey());

	propagation.Propagate();

	const auto* playerWorld = world.Transforms.TryGetWorld(player.TransformKey());
	const auto* tilemapWorld = world.Transforms.TryGetWorld(tilemap.TransformKey());
	ASSERT_NE(playerWorld, nullptr);
	ASSERT_NE(tilemapWorld, nullptr);

	EXPECT_NEAR(playerWorld->Position.X, 50.0f, 1e-5f);
	EXPECT_NEAR(playerWorld->Position.Y, 30.0f, 1e-5f);
	EXPECT_NEAR(tilemapWorld->Position.X, -100.0f, 1e-5f);
	EXPECT_NEAR(tilemapWorld->Position.Y, -100.0f, 1e-5f);
}

TEST(Tilemap2d, InheritsParentTransform)
{
	TransformPropagationFixture transformServices;
	auto& world = transformServices.GameWorld;
	auto& propagation = transformServices.Propagation;

	SceneNode2D camera(world, Transform2f({ 200.0f, 100.0f }, 0.0f, { 1.0f, 1.0f }));
	Tilemap2d tilemap(world,
		Transform2f({ 0.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }),
		32, 32, 8.0f);
	tilemap.SetTransformParent(camera.TransformKey());

	propagation.Propagate();

	const auto* tilemapWorld = world.Transforms.TryGetWorld(tilemap.TransformKey());
	ASSERT_NE(tilemapWorld, nullptr);
	EXPECT_NEAR(tilemapWorld->Position.X, 200.0f, 1e-5f);
	EXPECT_NEAR(tilemapWorld->Position.Y, 100.0f, 1e-5f);
}

TEST(Tilemap2d, DestructionFreesTransformSlot)
{
	TransformPropagationFixture transformServices;
	auto& world = transformServices.GameWorld;

	DataBatchKey key;
	{
		Tilemap2d tilemap(world,
			Transform2f({ 5.0f, 5.0f }, 0.0f, { 1.0f, 1.0f }),
			16, 16, 32.0f);
		key = tilemap.TransformKey();

		EXPECT_NE(world.Transforms.TryGetLocal(key), nullptr);
		EXPECT_NE(world.Transforms.TryGetWorld(key), nullptr);
	}

	EXPECT_EQ(world.Transforms.TryGetLocal(key), nullptr);
	EXPECT_EQ(world.Transforms.TryGetWorld(key), nullptr);
}

TEST(Tilemap2d, GridStorageRoundtrip)
{
	TransformPropagationFixture transformServices;
	auto& world = transformServices.GameWorld;

	Tilemap2d tilemap(world,
		Transform2f({ 0.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }),
		4, 3, 16.0f);

	EXPECT_EQ(tilemap.Width(), 4u);
	EXPECT_EQ(tilemap.Height(), 3u);
	EXPECT_EQ(tilemap.GetTile(2, 1), 0u);

	tilemap.SetTile(2, 1, 7);
	EXPECT_EQ(tilemap.GetTile(2, 1), 7u);
	EXPECT_EQ(tilemap.GetTile(0, 0), 0u);
}

// ============================================================================
// SceneNode<TTransform>
// ============================================================================

TEST(SceneNode2D, ConstructionAllocatesTransformSlot)
{
	TransformPropagationFixture fixture;
	auto& world = fixture.GameWorld;

	SceneNode2D node(world, Transform2f({ 1.0f, 2.0f }, 0.0f, { 1.0f, 1.0f }));

	const auto* local = world.Transforms.TryGetLocal(node.TransformKey());
	ASSERT_NE(local, nullptr);
	EXPECT_NEAR(local->Position.X, 1.0f, 1e-5f);
	EXPECT_NEAR(local->Position.Y, 2.0f, 1e-5f);
	EXPECT_NE(world.Transforms.TryGetWorld(node.TransformKey()), nullptr);
	EXPECT_FALSE(node.HasParent());
}

TEST(SceneNode2D, DestructionFreesTransformSlot)
{
	TransformPropagationFixture fixture;
	auto& world = fixture.GameWorld;

	DataBatchKey key;
	{
		SceneNode2D node(world, Transform2f({ 0.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }));
		key = node.TransformKey();
		EXPECT_NE(world.Transforms.TryGetLocal(key), nullptr);
	}

	EXPECT_EQ(world.Transforms.TryGetLocal(key), nullptr);
	EXPECT_EQ(world.Transforms.TryGetWorld(key), nullptr);
}

TEST(SceneNode2D, SetParentRoutesThroughHierarchy)
{
	TransformPropagationFixture fixture;
	auto& world = fixture.GameWorld;

	SceneNode2D parent(world, Transform2f({ 10.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }));
	SceneNode2D child(world, Transform2f({ 3.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }));

	child.SetParent(parent);

	EXPECT_TRUE(child.HasParent());
	EXPECT_EQ(child.GetParentKey().Value, parent.TransformKey().Value);

	child.ClearParent();
	EXPECT_FALSE(child.HasParent());
}

TEST(SceneNode2D, ParentingObservableViaPropagation)
{
	TransformPropagationFixture fixture;
	auto& world = fixture.GameWorld;
	auto& propagation = fixture.Propagation;

	SceneNode2D parent(world, Transform2f({ 10.0f, 20.0f }, 0.0f, { 1.0f, 1.0f }));
	SceneNode2D child(world, Transform2f({ 3.0f, 4.0f }, 0.0f, { 1.0f, 1.0f }));
	child.SetParent(parent);

	propagation.Propagate();

	const auto* childWorld = world.Transforms.TryGetWorld(child.TransformKey());
	ASSERT_NE(childWorld, nullptr);
	EXPECT_NEAR(childWorld->Position.X, 13.0f, 1e-5f);
	EXPECT_NEAR(childWorld->Position.Y, 24.0f, 1e-5f);
}

TEST(SceneNode2D, MoveTransfersOwnership)
{
	TransformPropagationFixture fixture;
	auto& world = fixture.GameWorld;

	SceneNode2D original(world, Transform2f({ 7.0f, 8.0f }, 0.0f, { 1.0f, 1.0f }));
	DataBatchKey key = original.TransformKey();

	SceneNode2D moved(std::move(original));

	EXPECT_EQ(moved.TransformKey().Value, key.Value);
	EXPECT_NE(world.Transforms.TryGetLocal(key), nullptr);
}

TEST(SceneNode3D, ConstructionAllocatesTransformSlot)
{
	TransformPropagation3Fixture fixture;
	auto& world = fixture.GameWorld;

	SceneNode3D node(world, Transform3f(
		Vec3d(1.0f, 2.0f, 3.0f),
		Quatf::Identity(),
		Vec3d::One()));

	const auto* local = world.Transforms.TryGetLocal(node.TransformKey());
	ASSERT_NE(local, nullptr);
	EXPECT_NEAR(local->Position.X, 1.0f, 1e-5f);
	EXPECT_NEAR(local->Position.Z, 3.0f, 1e-5f);
}

TEST(SceneNode3D, ParentingObservableViaPropagation)
{
	TransformPropagation3Fixture fixture;
	auto& world = fixture.GameWorld;
	auto& propagation = fixture.Propagation;

	SceneNode3D parent(world, Transform3f(
		Vec3d(10.0f, 20.0f, 30.0f),
		Quatf::Identity(),
		Vec3d::One()));
	SceneNode3D child(world, Transform3f(
		Vec3d(1.0f, 2.0f, 3.0f),
		Quatf::Identity(),
		Vec3d::One()));
	child.SetParent(parent);

	propagation.Propagate();

	const auto* childWorld = world.Transforms.TryGetWorld(child.TransformKey());
	ASSERT_NE(childWorld, nullptr);
	EXPECT_NEAR(childWorld->Position.X, 11.0f, 1e-5f);
	EXPECT_NEAR(childWorld->Position.Y, 22.0f, 1e-5f);
	EXPECT_NEAR(childWorld->Position.Z, 33.0f, 1e-5f);
}
