#pragma once

#include <math/geometry/2d/Aabb2d.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

//=============================================================================
// QuadTree<T>
//
// Header-only 2D broadphase spatial index keyed by an opaque user payload T
// and an Aabb2d. It is intentionally ignorant of how the caller produces
// bounds -- TransformNode, Transform2d, raw rects, tiles, anything.
//
// Storage model:
//   - Node pool in a std::vector<Node>. Children are indices, not pointers;
//     index 0 is always the root, so 0 doubles as the "no child" sentinel.
//   - Entry pool in a std::vector<Entry> with a free list for stable ids, so
//     EntryIds returned from Insert stay valid across Update / Remove.
//   - Entries that straddle a subdivision stay at the deepest node that still
//     fully contains them (classic non-loose quadtree). Updates re-home an
//     entry when it either escapes the current node or stops straddling and
//     can descend into an existing child.
//
// Subdivision happens on Insert when a leaf's entry count crosses the
// configured threshold and MaxDepth isn't reached. Clear() resets the active
// tree to a fresh root and is the intended path for dynamic broadphase
// rebuilds.
//
// The tree is a broadphase only -- it knows nothing about collision response.
// Callers drive queries, then run their own narrowphase over the candidates.
//=============================================================================

template <typename T>
class QuadTree
{
public:
	using EntryId = uint32_t;
	static constexpr EntryId InvalidEntry = std::numeric_limits<EntryId>::max();

	struct Config
	{
		Aabb2d RootBounds = Aabb2d(Vec2d(-1024.0f, -1024.0f), Vec2d(1024.0f, 1024.0f));
		int MaxDepth = 6;
		int MaxEntriesPerLeaf = 8;
	};

	explicit QuadTree(const Config& config = Config{})
		: Cfg(config)
	{
		Nodes.push_back(Node{config.RootBounds, 0});
	}

	EntryId Insert(const T& value, const Aabb2d& bounds)
	{
		const EntryId id = AllocateEntry();
		Entries[id].Value = value;
		Entries[id].Bounds = bounds;
		Entries[id].NodeIndex = InsertIntoNode(0, id, bounds);
		++EntryCount;
		return id;
	}

	void Update(EntryId id, const Aabb2d& newBounds)
	{
		Entry& entry = Entries[id];
		if (Contains(Nodes[entry.NodeIndex].Bounds, newBounds))
		{
			if (!Nodes[entry.NodeIndex].IsLeaf() && FindContainingChild(entry.NodeIndex, newBounds) >= 0)
			{
				const uint32_t reinsertRoot = entry.NodeIndex;
				RemoveFromNode(entry.NodeIndex, id);
				entry.Bounds = newBounds;
				entry.NodeIndex = InsertIntoNode(reinsertRoot, id, newBounds);
				return;
			}

			entry.Bounds = newBounds;
			return;
		}

		RemoveFromNode(entry.NodeIndex, id);
		entry.Bounds = newBounds;
		entry.NodeIndex = InsertIntoNode(0, id, newBounds);
	}

	void Remove(EntryId id)
	{
		Entry& entry = Entries[id];
		RemoveFromNode(entry.NodeIndex, id);
		FreeEntry(id);
		--EntryCount;
	}

	// Removes every entry and subdivision. EntryIds from before Clear() are
	// invalid afterward. Vector capacities are retained for rebuild-heavy use.
	void Clear()
	{
		Nodes.clear();
		Entries.clear();
		FreeHead = InvalidEntry;
		EntryCount = 0;
		Nodes.push_back(Node{Cfg.RootBounds, 0});
	}

	template <typename OutputIt>
	void Query(const Aabb2d& region, OutputIt out) const
	{
		QueryNode(0, region, out);
	}

	template <typename OutputIt>
	void QueryPoint(const Vec2d& point, OutputIt out) const
	{
		QueryPointNode(0, point, out);
	}

	// Visitor signature: void(const Aabb2d& bounds, int depth).
	// Visits every node, interior and leaf, in depth-first order.
	template <typename Visitor>
	void ForEachNode(Visitor&& visitor) const
	{
		ForEachNodeRec(0, 0, visitor);
	}

	size_t Count() const { return EntryCount; }
	size_t NodeCount() const { return Nodes.size(); }
	const Config& GetConfig() const { return Cfg; }
	const Aabb2d& GetEntryBounds(EntryId id) const { return Entries[id].Bounds; }
	const T& GetEntryValue(EntryId id) const { return Entries[id].Value; }
	const Aabb2d& GetEntryNodeBounds(EntryId id) const { return Nodes[Entries[id].NodeIndex].Bounds; }

private:
	struct Entry
	{
		T Value{};
		Aabb2d Bounds;
		uint32_t NodeIndex = 0;
		EntryId NextFree = InvalidEntry;
		bool Alive = false;
	};

	struct Node
	{
		Aabb2d Bounds;
		int Depth = 0;
		uint32_t Children[4] = {0, 0, 0, 0};
		std::vector<EntryId> Entries;

		bool IsLeaf() const { return Children[0] == 0; }
	};

	static bool Contains(const Aabb2d& outer, const Aabb2d& inner)
	{
		return inner.Min.X >= outer.Min.X && inner.Max.X <= outer.Max.X
			&& inner.Min.Y >= outer.Min.Y && inner.Max.Y <= outer.Max.Y;
	}

	int FindContainingChild(uint32_t nodeIdx, const Aabb2d& bounds) const
	{
		if (Nodes[nodeIdx].IsLeaf())
		{
			return -1;
		}

		for (int i = 0; i < 4; ++i)
		{
			const uint32_t childIdx = Nodes[nodeIdx].Children[i];
			if (Contains(Nodes[childIdx].Bounds, bounds))
			{
				return i;
			}
		}

		return -1;
	}

	EntryId AllocateEntry()
	{
		if (FreeHead != InvalidEntry)
		{
			const EntryId id = FreeHead;
			FreeHead = Entries[id].NextFree;
			Entries[id] = Entry{};
			Entries[id].Alive = true;
			return id;
		}
		Entries.push_back(Entry{});
		const EntryId id = static_cast<EntryId>(Entries.size() - 1);
		Entries[id].Alive = true;
		return id;
	}

	void FreeEntry(EntryId id)
	{
		Entries[id].Alive = false;
		Entries[id].NextFree = FreeHead;
		FreeHead = id;
	}

	uint32_t InsertIntoNode(uint32_t nodeIdx, EntryId entryId, const Aabb2d& bounds)
	{
		while (true)
		{
			const bool isLeaf = Nodes[nodeIdx].IsLeaf();
			const size_t entryCount = Nodes[nodeIdx].Entries.size();
			const int depth = Nodes[nodeIdx].Depth;

			if (isLeaf
				&& entryCount >= static_cast<size_t>(Cfg.MaxEntriesPerLeaf)
				&& depth < Cfg.MaxDepth)
			{
				Subdivide(nodeIdx);
			}

			if (Nodes[nodeIdx].IsLeaf())
			{
				Nodes[nodeIdx].Entries.push_back(entryId);
				return nodeIdx;
			}

			int fitChild = FindContainingChild(nodeIdx, bounds);
			if (fitChild < 0)
			{
				Nodes[nodeIdx].Entries.push_back(entryId);
				return nodeIdx;
			}

			nodeIdx = Nodes[nodeIdx].Children[fitChild];
		}
	}

	void Subdivide(uint32_t nodeIdx)
	{
		const Aabb2d bounds = Nodes[nodeIdx].Bounds;
		const int depth = Nodes[nodeIdx].Depth;
		const Vec2d center = bounds.Center();
		const Vec2d min = bounds.Min;
		const Vec2d max = bounds.Max;

		const uint32_t base = static_cast<uint32_t>(Nodes.size());
		Nodes.push_back(Node{Aabb2d(Vec2d(min.X, min.Y), Vec2d(center.X, center.Y)), depth + 1});
		Nodes.push_back(Node{Aabb2d(Vec2d(center.X, min.Y), Vec2d(max.X, center.Y)), depth + 1});
		Nodes.push_back(Node{Aabb2d(Vec2d(min.X, center.Y), Vec2d(center.X, max.Y)), depth + 1});
		Nodes.push_back(Node{Aabb2d(Vec2d(center.X, center.Y), Vec2d(max.X, max.Y)), depth + 1});

		for (int i = 0; i < 4; ++i)
		{
			Nodes[nodeIdx].Children[i] = base + i;
		}

		std::vector<EntryId> retained;
		retained.reserve(Nodes[nodeIdx].Entries.size());

		const std::vector<EntryId> existing = std::move(Nodes[nodeIdx].Entries);
		for (EntryId eid : existing)
		{
			const Aabb2d& eb = Entries[eid].Bounds;
			int fit = -1;
			for (int i = 0; i < 4; ++i)
			{
				const uint32_t childIdx = Nodes[nodeIdx].Children[i];
				if (Contains(Nodes[childIdx].Bounds, eb))
				{
					fit = i;
					break;
				}
			}
			if (fit >= 0)
			{
				const uint32_t childIdx = Nodes[nodeIdx].Children[fit];
				Nodes[childIdx].Entries.push_back(eid);
				Entries[eid].NodeIndex = childIdx;
			}
			else
			{
				retained.push_back(eid);
			}
		}
		Nodes[nodeIdx].Entries = std::move(retained);
	}

	void RemoveFromNode(uint32_t nodeIdx, EntryId id)
	{
		auto& vec = Nodes[nodeIdx].Entries;
		for (size_t i = 0; i < vec.size(); ++i)
		{
			if (vec[i] == id)
			{
				vec[i] = vec.back();
				vec.pop_back();
				return;
			}
		}
	}

	template <typename OutputIt>
	void QueryNode(uint32_t nodeIdx, const Aabb2d& region, OutputIt& out) const
	{
		const Node& node = Nodes[nodeIdx];
		if (!node.Bounds.Intersects(region))
		{
			return;
		}

		for (EntryId id : node.Entries)
		{
			if (Entries[id].Bounds.Intersects(region))
			{
				*out++ = Entries[id].Value;
			}
		}

		if (!node.IsLeaf())
		{
			for (int i = 0; i < 4; ++i)
			{
				QueryNode(node.Children[i], region, out);
			}
		}
	}

	template <typename OutputIt>
	void QueryPointNode(uint32_t nodeIdx, const Vec2d& point, OutputIt& out) const
	{
		const Node& node = Nodes[nodeIdx];
		if (!node.Bounds.Contains(point))
		{
			return;
		}

		for (EntryId id : node.Entries)
		{
			if (Entries[id].Bounds.Contains(point))
			{
				*out++ = Entries[id].Value;
			}
		}

		if (!node.IsLeaf())
		{
			for (int i = 0; i < 4; ++i)
			{
				QueryPointNode(node.Children[i], point, out);
			}
		}
	}

	template <typename Visitor>
	void ForEachNodeRec(uint32_t nodeIdx, int depth, Visitor& visitor) const
	{
		const Node& node = Nodes[nodeIdx];
		visitor(node.Bounds, depth);
		if (!node.IsLeaf())
		{
			for (int i = 0; i < 4; ++i)
			{
				ForEachNodeRec(node.Children[i], depth + 1, visitor);
			}
		}
	}

	Config Cfg;
	std::vector<Node> Nodes;
	std::vector<Entry> Entries;
	EntryId FreeHead = InvalidEntry;
	size_t EntryCount = 0;
};
