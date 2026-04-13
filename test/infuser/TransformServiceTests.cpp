#include <gtest/gtest.h>
#include <batch/DataBatch.h>
#include <math/Transform2.h>
#include <math/Transform3.h>
#include <primitive/transform/core/TransformDefaults.h>
#include <primitive/transform/hierarchy/TransformHierarchyService.h>
#include <primitive/transform/hierarchy/TransformPropagationOrderService.h>
#include <primitive/transform/hierarchy/TransformPropagationSystem.h>
#include <raii/DataBatchHandle.h>
#include <service/ServiceHost.h>
#include <service/ServiceProvider.h>
#include <cmath>
#include <numbers>

namespace
{
	using Transform2Hierarchy = TransformHierarchyService<TransformDefaults::Tags::Transform2DTag>;
	using Transform2PropagationOrder = TransformPropagationOrderService<TransformDefaults::Tags::Transform2DTag>;
	using Transform2Propagation = TransformPropagationSystem<
		Transform2f,
		TransformDefaults::Tags::Transform2DTag>;
	using Transform3Hierarchy = TransformHierarchyService<TransformDefaults::Tags::Transform3DTag>;
	using Transform3PropagationOrder = TransformPropagationOrderService<TransformDefaults::Tags::Transform3DTag>;
	using Transform3Propagation = TransformPropagationSystem<
		Transform3f,
		TransformDefaults::Tags::Transform3DTag>;
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
	DataBatch<Transform2f>& Locals;
	DataBatch<Transform2f>& Worlds;
	Transform2Hierarchy& Hierarchy;
	Transform2PropagationOrder& PropagationOrder;
	ServiceProvider Provider;
	Transform2Propagation Propagation;

	TransformPropagationFixture()
		: Locals(Host.AddTaggedService<
			DataBatch<Transform2f>,
			TransformDefaults::Tags::LocalTransformTag>())
		, Worlds(Host.AddTaggedService<
			DataBatch<Transform2f>,
			TransformDefaults::Tags::WorldTransformTag>())
		, Hierarchy(Host.AddService<Transform2Hierarchy>())
		, PropagationOrder(Host.AddService<Transform2PropagationOrder>())
		, Provider(Host)
		, Propagation(Provider)
	{
	}
};

struct TransformPropagation3Fixture
{
	ServiceHost Host;
	DataBatch<Transform3f>& Locals;
	DataBatch<Transform3f>& Worlds;
	Transform3Hierarchy& Hierarchy;
	Transform3PropagationOrder& PropagationOrder;
	ServiceProvider Provider;
	Transform3Propagation Propagation;

	TransformPropagation3Fixture()
		: Locals(Host.AddTaggedService<
			DataBatch<Transform3f>,
			TransformDefaults::Tags::LocalTransformTag>())
		, Worlds(Host.AddTaggedService<
			DataBatch<Transform3f>,
			TransformDefaults::Tags::WorldTransformTag>())
		, Hierarchy(Host.AddService<Transform3Hierarchy>())
		, PropagationOrder(Host.AddService<Transform3PropagationOrder>())
		, Provider(Host)
		, Propagation(Provider)
	{
	}
};

TEST(TransformPropagation, RootWorldEqualsLocal2D)
{
	TransformPropagationFixture transformServices;
	auto& locals = transformServices.Locals;
	auto& worlds = transformServices.Worlds;
	auto& hierarchy = transformServices.Hierarchy;
	auto& propagation = transformServices.Propagation;

	Transform2f localTransform({ 10.0f, 20.0f }, 0.0f, { 1.0f, 1.0f });
	auto localHandle = locals.Emplace(localTransform);
	auto worldHandle = worlds.Emplace(Transform2f::Identity());

	DataBatchKey key = localHandle.GetToken();

	// World handle must use the same key — emplace into worlds with same key.
	// Since DataBatch assigns keys sequentially and both batches start at 1,
	// the first emplace in each yields the same key. This is the expected
	// allocation pattern for paired local/world transforms.
	ASSERT_EQ(worldHandle.GetToken().Value, key.Value);

	hierarchy.Register(key);
	propagation.Propagate();

	const auto* world = worlds.TryGet(key);
	ASSERT_NE(world, nullptr);
	EXPECT_TRUE(world->NearlyEquals(localTransform));
}

TEST(TransformPropagation, OrderCacheSurvivesSystemRecreation)
{
	ServiceHost host;
	auto& locals = host.AddTaggedService<
		DataBatch<Transform2f>,
		TransformDefaults::Tags::LocalTransformTag>();
	auto& worlds = host.AddTaggedService<
		DataBatch<Transform2f>,
		TransformDefaults::Tags::WorldTransformTag>();
	auto& hierarchy = host.AddService<Transform2Hierarchy>();
	auto& cache = host.AddService<Transform2PropagationOrder>();
	ServiceProvider provider(host);

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
		Transform2Propagation propagation(provider);
		propagation.Propagate();
	}

	auto cachedOrder = cache.GetOrder();
	ASSERT_EQ(cachedOrder.size(), 2u);
	const auto* cachedOrderStorage = cachedOrder.data();

	{
		Transform2Propagation recreatedPropagation(provider);
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

	// Child at (10, 0) in parent space — should end up at (0, 10) in world
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

	// Child position in world: parent transforms (10, 0) by scale 2 → (20, 0)
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
		Vec3::Zero(),
		Quatf::FromAxisAngle(Vec3(0.0f, 0.0f, 1.0f), halfPi),
		Vec3::One());
	auto parentLocalH = locals.Emplace(parentLocal);
	auto parentWorldH = worlds.Emplace(Transform3f::Identity());
	DataBatchKey parentKey = parentLocalH.GetToken();

	Transform3f childLocal(
		Vec3(10.0f, 0.0f, 0.0f),
		Quatf::Identity(),
		Vec3::One());
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
// Proof: non-node participation
//
// Demonstrates that a tilemap-like object can own transform handles and
// register with the transform hierarchy without being a SceneNode2D.
// ============================================================================

// Minimal SceneNode2D sketch — owns node-graph links AND transform handles.
struct SceneNode2DSketch
{
	// Transform handles — RAII ownership of the hot data slots
	DataBatchHandle<Transform2f> LocalTransformHandle;
	DataBatchHandle<Transform2f> WorldTransformHandle;

	// The stable key shared between local/world batches and the hierarchy
	DataBatchKey TransformKey() const { return LocalTransformHandle.GetToken(); }

	// Node-graph parent/child (separate from transform hierarchy)
	SceneNode2DSketch* NodeParent = nullptr;
	std::vector<SceneNode2DSketch*> NodeChildren;

	SceneNode2DSketch(
		DataBatch<Transform2f>& locals,
		DataBatch<Transform2f>& worlds,
		Transform2Hierarchy& hierarchy,
		const Transform2f& localTransform)
	{
		LocalTransformHandle = locals.Emplace(localTransform);
		WorldTransformHandle = worlds.Emplace(Transform2f::Identity());
		hierarchy.Register(TransformKey());
	}

	void SetTransformParent(Transform2Hierarchy& hierarchy, SceneNode2DSketch& parent)
	{
		hierarchy.SetParent(TransformKey(), parent.TransformKey());
	}

	~SceneNode2DSketch() = default;
};

// Minimal tilemap sketch — NOT a node. Just owns a transform.
struct TilemapSketch
{
	DataBatchHandle<Transform2f> LocalTransformHandle;
	DataBatchHandle<Transform2f> WorldTransformHandle;

	DataBatchKey TransformKey() const { return LocalTransformHandle.GetToken(); }

	// Tilemap-specific data (grid dimensions, tile data, etc.)
	int GridWidth = 0;
	int GridHeight = 0;

	TilemapSketch(
		DataBatch<Transform2f>& locals,
		DataBatch<Transform2f>& worlds,
		Transform2Hierarchy& hierarchy,
		const Transform2f& origin,
		int gridW, int gridH)
		: GridWidth(gridW), GridHeight(gridH)
	{
		LocalTransformHandle = locals.Emplace(origin);
		WorldTransformHandle = worlds.Emplace(Transform2f::Identity());
		hierarchy.Register(TransformKey());
	}

	void SetTransformParent(Transform2Hierarchy& hierarchy, DataBatchKey parentKey)
	{
		hierarchy.SetParent(TransformKey(), parentKey);
	}
};

TEST(TransformArchitecture, SceneNodeAndTilemapCoexist)
{
	TransformPropagationFixture transformServices;
	auto& locals = transformServices.Locals;
	auto& worlds = transformServices.Worlds;
	auto& hierarchy = transformServices.Hierarchy;
	auto& propagation = transformServices.Propagation;

	// Create a scene node as the level root
	SceneNode2DSketch levelRoot(locals, worlds, hierarchy,
		Transform2f({ 0.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }));

	// Create a child scene node
	SceneNode2DSketch player(locals, worlds, hierarchy,
		Transform2f({ 50.0f, 30.0f }, 0.0f, { 1.0f, 1.0f }));
	player.SetTransformParent(hierarchy, levelRoot);

	// Create a tilemap (NOT a node) parented under the same level root
	TilemapSketch tilemap(locals, worlds, hierarchy,
		Transform2f({ -100.0f, -100.0f }, 0.0f, { 1.0f, 1.0f }),
		64, 64);
	tilemap.SetTransformParent(hierarchy, levelRoot.TransformKey());

	// Propagate — both node and non-node participate equally
	propagation.Propagate();

	const auto* playerWorld = worlds.TryGet(player.TransformKey());
	const auto* tilemapWorld = worlds.TryGet(tilemap.TransformKey());

	ASSERT_NE(playerWorld, nullptr);
	ASSERT_NE(tilemapWorld, nullptr);

	// Player: root (0,0) + player (50,30) = (50, 30)
	EXPECT_NEAR(playerWorld->Position.X, 50.0f, 1e-5f);
	EXPECT_NEAR(playerWorld->Position.Y, 30.0f, 1e-5f);

	// Tilemap: root (0,0) + tilemap (-100,-100) = (-100, -100)
	EXPECT_NEAR(tilemapWorld->Position.X, -100.0f, 1e-5f);
	EXPECT_NEAR(tilemapWorld->Position.Y, -100.0f, 1e-5f);
}

TEST(TransformArchitecture, TilemapParentedUnderNode)
{
	TransformPropagationFixture transformServices;
	auto& locals = transformServices.Locals;
	auto& worlds = transformServices.Worlds;
	auto& hierarchy = transformServices.Hierarchy;
	auto& propagation = transformServices.Propagation;

	// Node at (200, 100)
	SceneNode2DSketch camera(locals, worlds, hierarchy,
		Transform2f({ 200.0f, 100.0f }, 0.0f, { 1.0f, 1.0f }));

	// Tilemap parented under the node
	TilemapSketch tilemap(locals, worlds, hierarchy,
		Transform2f({ 0.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }),
		32, 32);
	tilemap.SetTransformParent(hierarchy, camera.TransformKey());

	propagation.Propagate();

	const auto* tilemapWorld = worlds.TryGet(tilemap.TransformKey());
	ASSERT_NE(tilemapWorld, nullptr);

	// Tilemap inherits camera's position
	EXPECT_NEAR(tilemapWorld->Position.X, 200.0f, 1e-5f);
	EXPECT_NEAR(tilemapWorld->Position.Y, 100.0f, 1e-5f);
}

TEST(TransformArchitecture, HandleDestructionCleansUpTransformSlot)
{
	DataBatch<Transform2f> locals;
	DataBatch<Transform2f> worlds;
	Transform2Hierarchy hierarchy;

	DataBatchKey key;
	{
		TilemapSketch tilemap(locals, worlds, hierarchy,
			Transform2f({ 5.0f, 5.0f }, 0.0f, { 1.0f, 1.0f }),
			16, 16);
		key = tilemap.TransformKey();

		EXPECT_TRUE(locals.Contains(key));
		EXPECT_TRUE(worlds.Contains(key));
		// Tilemap goes out of scope — handles destroy, slots freed
	}

	EXPECT_FALSE(locals.Contains(key));
	EXPECT_FALSE(worlds.Contains(key));
}

TEST(TransformArchitecture, PairedKeysMatchWhenAllocatedTogether)
{
	// This test verifies the expected allocation pattern:
	// when you emplace into locals and worlds in lockstep from a fresh
	// state, the keys match because both batches use sequential counters.
	DataBatch<Transform2f> locals;
	DataBatch<Transform2f> worlds;

	auto lh1 = locals.Emplace(Transform2f::Identity());
	auto wh1 = worlds.Emplace(Transform2f::Identity());
	EXPECT_EQ(lh1.GetToken().Value, wh1.GetToken().Value);

	auto lh2 = locals.Emplace(Transform2f::Identity());
	auto wh2 = worlds.Emplace(Transform2f::Identity());
	EXPECT_EQ(lh2.GetToken().Value, wh2.GetToken().Value);
}
