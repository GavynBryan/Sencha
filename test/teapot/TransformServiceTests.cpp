#include <gtest/gtest.h>
#include <batch/DataBatch.h>
#include <transform/TransformHierarchy2Service.h>
#include <transform/TransformPropagation2System.h>
#include <math/Transform2.h>
#include <cmath>
#include <numbers>

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
// TransformHierarchy2Service
// ============================================================================

TEST(TransformHierarchy2, RegisterAndQueryRoots)
{
	TransformHierarchy2Service hierarchy;

	DataBatchKey a{ 1 };
	DataBatchKey b{ 2 };

	hierarchy.Register(a);
	hierarchy.Register(b);

	auto roots = hierarchy.GetRoots();
	EXPECT_EQ(roots.size(), 2u);
	EXPECT_TRUE(hierarchy.IsRegistered(a));
	EXPECT_TRUE(hierarchy.IsRegistered(b));
}

TEST(TransformHierarchy2, SetParentCreatesRelationship)
{
	TransformHierarchy2Service hierarchy;

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

TEST(TransformHierarchy2, SetParentAutoRegisters)
{
	TransformHierarchy2Service hierarchy;

	DataBatchKey parent{ 1 };
	DataBatchKey child{ 2 };

	// SetParent should register the child and parent implicitly
	hierarchy.Register(child);
	hierarchy.SetParent(child, parent);

	EXPECT_TRUE(hierarchy.IsRegistered(parent));
	EXPECT_TRUE(hierarchy.IsRegistered(child));
}

TEST(TransformHierarchy2, ClearParentOrphansChild)
{
	TransformHierarchy2Service hierarchy;

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

TEST(TransformHierarchy2, ReparentMovesChild)
{
	TransformHierarchy2Service hierarchy;

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

TEST(TransformHierarchy2, UnregisterOrphansChildren)
{
	TransformHierarchy2Service hierarchy;

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

TEST(TransformHierarchy2, RootsExcludesParentedKeys)
{
	TransformHierarchy2Service hierarchy;

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
// TransformPropagation2System
// ============================================================================

TEST(TransformPropagation2, RootWorldEqualsLocal)
{
	DataBatch<Transform2f> locals;
	DataBatch<Transform2f> worlds;
	TransformHierarchy2Service hierarchy;
	TransformPropagation2System propagation(locals, worlds, hierarchy);

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

TEST(TransformPropagation2, ChildInheritsParentTransform)
{
	DataBatch<Transform2f> locals;
	DataBatch<Transform2f> worlds;
	TransformHierarchy2Service hierarchy;
	TransformPropagation2System propagation(locals, worlds, hierarchy);

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

TEST(TransformPropagation2, RotatedParentAffectsChildPosition)
{
	DataBatch<Transform2f> locals;
	DataBatch<Transform2f> worlds;
	TransformHierarchy2Service hierarchy;
	TransformPropagation2System propagation(locals, worlds, hierarchy);

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
	EXPECT_NEAR(childWorld->Position.Data[0], 0.0f, 1e-5f);
	EXPECT_NEAR(childWorld->Position.Data[1], 10.0f, 1e-5f);
	EXPECT_NEAR(childWorld->Rotation, halfPi, 1e-5f);
}

TEST(TransformPropagation2, ThreeLevelHierarchy)
{
	DataBatch<Transform2f> locals;
	DataBatch<Transform2f> worlds;
	TransformHierarchy2Service hierarchy;
	TransformPropagation2System propagation(locals, worlds, hierarchy);

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

TEST(TransformPropagation2, ScaleComposes)
{
	DataBatch<Transform2f> locals;
	DataBatch<Transform2f> worlds;
	TransformHierarchy2Service hierarchy;
	TransformPropagation2System propagation(locals, worlds, hierarchy);

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
	EXPECT_NEAR(childWorld->Position.Data[0], 20.0f, 1e-5f);
	EXPECT_NEAR(childWorld->Position.Data[1], 0.0f, 1e-5f);
	// Scale composes: 2 * 0.5 = 1
	EXPECT_NEAR(childWorld->Scale.Data[0], 1.0f, 1e-5f);
	EXPECT_NEAR(childWorld->Scale.Data[1], 1.0f, 1e-5f);
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
	LifetimeHandle<DataBatchKey> LocalTransformHandle;
	LifetimeHandle<DataBatchKey> WorldTransformHandle;

	// The stable key shared between local/world batches and the hierarchy
	DataBatchKey TransformKey() const { return LocalTransformHandle.GetToken(); }

	// Node-graph parent/child (separate from transform hierarchy)
	SceneNode2DSketch* NodeParent = nullptr;
	std::vector<SceneNode2DSketch*> NodeChildren;

	SceneNode2DSketch(
		DataBatch<Transform2f>& locals,
		DataBatch<Transform2f>& worlds,
		TransformHierarchy2Service& hierarchy,
		const Transform2f& localTransform)
	{
		LocalTransformHandle = locals.Emplace(localTransform);
		WorldTransformHandle = worlds.Emplace(Transform2f::Identity());
		hierarchy.Register(TransformKey());
	}

	void SetTransformParent(TransformHierarchy2Service& hierarchy, SceneNode2DSketch& parent)
	{
		hierarchy.SetParent(TransformKey(), parent.TransformKey());
	}

	~SceneNode2DSketch() = default;
};

// Minimal tilemap sketch — NOT a node. Just owns a transform.
struct TilemapSketch
{
	LifetimeHandle<DataBatchKey> LocalTransformHandle;
	LifetimeHandle<DataBatchKey> WorldTransformHandle;

	DataBatchKey TransformKey() const { return LocalTransformHandle.GetToken(); }

	// Tilemap-specific data (grid dimensions, tile data, etc.)
	int GridWidth = 0;
	int GridHeight = 0;

	TilemapSketch(
		DataBatch<Transform2f>& locals,
		DataBatch<Transform2f>& worlds,
		TransformHierarchy2Service& hierarchy,
		const Transform2f& origin,
		int gridW, int gridH)
		: GridWidth(gridW), GridHeight(gridH)
	{
		LocalTransformHandle = locals.Emplace(origin);
		WorldTransformHandle = worlds.Emplace(Transform2f::Identity());
		hierarchy.Register(TransformKey());
	}

	void SetTransformParent(TransformHierarchy2Service& hierarchy, DataBatchKey parentKey)
	{
		hierarchy.SetParent(TransformKey(), parentKey);
	}
};

TEST(TransformArchitecture, SceneNodeAndTilemapCoexist)
{
	DataBatch<Transform2f> locals;
	DataBatch<Transform2f> worlds;
	TransformHierarchy2Service hierarchy;
	TransformPropagation2System propagation(locals, worlds, hierarchy);

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
	EXPECT_NEAR(playerWorld->Position.Data[0], 50.0f, 1e-5f);
	EXPECT_NEAR(playerWorld->Position.Data[1], 30.0f, 1e-5f);

	// Tilemap: root (0,0) + tilemap (-100,-100) = (-100, -100)
	EXPECT_NEAR(tilemapWorld->Position.Data[0], -100.0f, 1e-5f);
	EXPECT_NEAR(tilemapWorld->Position.Data[1], -100.0f, 1e-5f);
}

TEST(TransformArchitecture, TilemapParentedUnderNode)
{
	DataBatch<Transform2f> locals;
	DataBatch<Transform2f> worlds;
	TransformHierarchy2Service hierarchy;
	TransformPropagation2System propagation(locals, worlds, hierarchy);

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
	EXPECT_NEAR(tilemapWorld->Position.Data[0], 200.0f, 1e-5f);
	EXPECT_NEAR(tilemapWorld->Position.Data[1], 100.0f, 1e-5f);
}

TEST(TransformArchitecture, HandleDestructionCleansUpTransformSlot)
{
	DataBatch<Transform2f> locals;
	DataBatch<Transform2f> worlds;
	TransformHierarchy2Service hierarchy;

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
