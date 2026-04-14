#include <gtest/gtest.h>
#include <math/spatial/QuadTree.h>

#include <algorithm>
#include <iterator>
#include <vector>

namespace
{
	using IntTree = QuadTree<int>;

	Aabb2d MakeBox(float cx, float cy, float half = 1.0f)
	{
		return Aabb2d(Vec2d(cx - half, cy - half), Vec2d(cx + half, cy + half));
	}

	IntTree::Config DefaultConfig()
	{
		IntTree::Config cfg;
		cfg.RootBounds = Aabb2d(Vec2d(-100.0f, -100.0f), Vec2d(100.0f, 100.0f));
		cfg.MaxDepth = 5;
		cfg.MaxEntriesPerLeaf = 2;
		return cfg;
	}
}

TEST(QuadTree, EmptyTreeHasOneRootNodeAndZeroEntries)
{
	IntTree tree(DefaultConfig());
	EXPECT_EQ(tree.Count(), 0u);
	EXPECT_EQ(tree.NodeCount(), 1u);

	std::vector<int> hits;
	tree.Query(Aabb2d(Vec2d(-50, -50), Vec2d(50, 50)), std::back_inserter(hits));
	EXPECT_TRUE(hits.empty());
}

TEST(QuadTree, InsertThenQueryReturnsAllOverlappingEntries)
{
	IntTree tree(DefaultConfig());
	tree.Insert(1, MakeBox(10, 10));
	tree.Insert(2, MakeBox(-10, -10));
	tree.Insert(3, MakeBox(10, -10));

	std::vector<int> hits;
	tree.Query(Aabb2d(Vec2d(0, 0), Vec2d(20, 20)), std::back_inserter(hits));
	std::sort(hits.begin(), hits.end());
	ASSERT_EQ(hits.size(), 1u);
	EXPECT_EQ(hits[0], 1);

	hits.clear();
	tree.Query(Aabb2d(Vec2d(-50, -50), Vec2d(50, 50)), std::back_inserter(hits));
	std::sort(hits.begin(), hits.end());
	ASSERT_EQ(hits.size(), 3u);
	EXPECT_EQ(hits[0], 1);
	EXPECT_EQ(hits[1], 2);
	EXPECT_EQ(hits[2], 3);
}

TEST(QuadTree, QueryPointReturnsContainingEntries)
{
	IntTree tree(DefaultConfig());
	tree.Insert(1, MakeBox(10, 10, 5));
	tree.Insert(2, MakeBox(10, 10, 2));
	tree.Insert(3, MakeBox(-10, -10));

	std::vector<int> hits;
	tree.QueryPoint(Vec2d(10, 10), std::back_inserter(hits));
	std::sort(hits.begin(), hits.end());
	ASSERT_EQ(hits.size(), 2u);
	EXPECT_EQ(hits[0], 1);
	EXPECT_EQ(hits[1], 2);
}

TEST(QuadTree, InsertionTriggersSubdivisionWhenLeafOverfills)
{
	IntTree tree(DefaultConfig());
	tree.Insert(1, MakeBox(10, 10));
	tree.Insert(2, MakeBox(-10, 10));
	EXPECT_EQ(tree.NodeCount(), 1u);

	tree.Insert(3, MakeBox(10, -10));
	EXPECT_GT(tree.NodeCount(), 1u);
}

TEST(QuadTree, StraddlingEntryStaysAtParentAndIsStillQueryable)
{
	IntTree tree(DefaultConfig());
	// Force subdivision with two tiny entries in distinct quadrants.
	tree.Insert(1, MakeBox(50, 50, 1));
	tree.Insert(2, MakeBox(-50, -50, 1));
	tree.Insert(3, MakeBox(50, -50, 1));
	ASSERT_GT(tree.NodeCount(), 1u);

	// This straddles the x-axis and y-axis splits of the root, so it must
	// land at the root node.
	const IntTree::EntryId straddler = tree.Insert(99, MakeBox(0, 0, 5));

	std::vector<int> hits;
	tree.Query(Aabb2d(Vec2d(-1, -1), Vec2d(1, 1)), std::back_inserter(hits));
	EXPECT_NE(std::find(hits.begin(), hits.end(), 99), hits.end());
	(void)straddler;
}

TEST(QuadTree, UpdateMovesEntryToNewRegion)
{
	IntTree tree(DefaultConfig());
	tree.Insert(1, MakeBox(50, 50, 1));
	tree.Insert(2, MakeBox(-50, -50, 1));
	const IntTree::EntryId moving = tree.Insert(3, MakeBox(50, -50, 1));

	tree.Update(moving, MakeBox(-50, 50, 1));

	std::vector<int> hits;
	tree.Query(Aabb2d(Vec2d(-60, 40), Vec2d(-40, 60)), std::back_inserter(hits));
	std::sort(hits.begin(), hits.end());
	ASSERT_EQ(hits.size(), 1u);
	EXPECT_EQ(hits[0], 3);

	hits.clear();
	tree.Query(Aabb2d(Vec2d(40, -60), Vec2d(60, -40)), std::back_inserter(hits));
	EXPECT_TRUE(hits.empty());
}

TEST(QuadTree, UpdateRehomesParentEntryWhenItNoLongerStraddles)
{
	IntTree tree(DefaultConfig());
	tree.Insert(1, MakeBox(50, 50, 1));
	tree.Insert(2, MakeBox(-50, -50, 1));
	tree.Insert(3, MakeBox(50, -50, 1));
	ASSERT_GT(tree.NodeCount(), 1u);

	const IntTree::EntryId moving = tree.Insert(99, MakeBox(0, 0, 5));
	EXPECT_EQ(tree.GetEntryNodeBounds(moving), tree.GetConfig().RootBounds);

	tree.Update(moving, MakeBox(75, 75, 1));

	const Aabb2d& nodeBounds = tree.GetEntryNodeBounds(moving);
	EXPECT_NE(nodeBounds, tree.GetConfig().RootBounds);
	EXPECT_TRUE(nodeBounds.Contains(Vec2d(75, 75)));
	EXPECT_LT(nodeBounds.Size().X, tree.GetConfig().RootBounds.Size().X);
	EXPECT_LT(nodeBounds.Size().Y, tree.GetConfig().RootBounds.Size().Y);
}

TEST(QuadTree, UpdateInPlaceKeepsCountStable)
{
	IntTree tree(DefaultConfig());
	const IntTree::EntryId id = tree.Insert(1, MakeBox(10, 10, 1));
	const size_t nodesBefore = tree.NodeCount();

	tree.Update(id, MakeBox(10, 10, 2));

	EXPECT_EQ(tree.Count(), 1u);
	EXPECT_EQ(tree.NodeCount(), nodesBefore);
}

TEST(QuadTree, RemoveTakesEntryOutOfQueries)
{
	IntTree tree(DefaultConfig());
	tree.Insert(1, MakeBox(50, 50, 1));
	const IntTree::EntryId target = tree.Insert(2, MakeBox(-50, -50, 1));
	tree.Insert(3, MakeBox(50, -50, 1));

	tree.Remove(target);
	EXPECT_EQ(tree.Count(), 2u);

	std::vector<int> hits;
	tree.Query(Aabb2d(Vec2d(-60, -60), Vec2d(-40, -40)), std::back_inserter(hits));
	EXPECT_TRUE(hits.empty());
}

TEST(QuadTree, ReinsertAfterRemoveReusesFreeListSlot)
{
	IntTree tree(DefaultConfig());
	const IntTree::EntryId a = tree.Insert(1, MakeBox(10, 10));
	tree.Remove(a);
	const IntTree::EntryId b = tree.Insert(2, MakeBox(-10, -10));
	EXPECT_EQ(a, b);
}

TEST(QuadTree, ClearRemovesEntriesAndSubdivisionsForRebuild)
{
	IntTree tree(DefaultConfig());
	tree.Insert(1, MakeBox(50, 50, 1));
	tree.Insert(2, MakeBox(-50, -50, 1));
	tree.Insert(3, MakeBox(50, -50, 1));
	ASSERT_GT(tree.NodeCount(), 1u);

	tree.Clear();

	EXPECT_EQ(tree.Count(), 0u);
	EXPECT_EQ(tree.NodeCount(), 1u);

	std::vector<int> hits;
	tree.Query(Aabb2d(Vec2d(-100, -100), Vec2d(100, 100)), std::back_inserter(hits));
	EXPECT_TRUE(hits.empty());

	tree.Insert(42, MakeBox(10, 10));
	tree.Query(Aabb2d(Vec2d(0, 0), Vec2d(20, 20)), std::back_inserter(hits));
	ASSERT_EQ(hits.size(), 1u);
	EXPECT_EQ(hits[0], 42);
}

TEST(QuadTree, ForEachNodeVisitsRootAndSubdivisions)
{
	IntTree tree(DefaultConfig());
	tree.Insert(1, MakeBox(50, 50, 1));
	tree.Insert(2, MakeBox(-50, -50, 1));
	tree.Insert(3, MakeBox(50, -50, 1));

	size_t visitedNodes = 0;
	int maxDepthSeen = -1;
	tree.ForEachNode([&](const Aabb2d& bounds, int depth)
	{
		EXPECT_TRUE(bounds.IsValid());
		++visitedNodes;
		maxDepthSeen = std::max(maxDepthSeen, depth);
	});

	EXPECT_EQ(visitedNodes, tree.NodeCount());
	EXPECT_GE(maxDepthSeen, 1);
}

TEST(QuadTree, DeepTreeClampsAtMaxDepth)
{
	IntTree::Config cfg;
	cfg.RootBounds = Aabb2d(Vec2d(-100.0f, -100.0f), Vec2d(100.0f, 100.0f));
	cfg.MaxDepth = 2;
	cfg.MaxEntriesPerLeaf = 1;
	IntTree tree(cfg);

	// Cram many entries into one tiny corner so subdivisions would keep going
	// forever if MaxDepth weren't honoured.
	for (int i = 0; i < 32; ++i)
	{
		tree.Insert(i, MakeBox(-90.0f, -90.0f, 0.1f));
	}

	int deepest = 0;
	tree.ForEachNode([&](const Aabb2d&, int depth)
	{
		deepest = std::max(deepest, depth);
	});
	EXPECT_LE(deepest, cfg.MaxDepth);

	std::vector<int> hits;
	tree.Query(Aabb2d(Vec2d(-91, -91), Vec2d(-89, -89)), std::back_inserter(hits));
	EXPECT_EQ(hits.size(), 32u);
}
