#include <navigation/NavQueryContext.h>

#include "NavQueryContextBackend.h"

#include <DetourAlloc.h>

#include <algorithm>
#include <bit>
#include <tuple>

namespace
{
    // A fixed total order, so equal-cost ties never depend on insertion order.
    bool Before(const NavSearchNode& a, const NavSearchNode& b)
    {
        return std::tie(a.Estimate, a.Cost, a.Polygon) < std::tie(b.Estimate, b.Cost, b.Polygon);
    }

    std::uint32_t Slot(std::uint64_t polygon, std::uint32_t mask)
    {
        return static_cast<std::uint32_t>((polygon * 0x9E3779B97F4A7C15ull) >> 32) & mask;
    }
}

NavQueryContext::Backend::Backend(const NavQueryContextConfig& config)
    : Config(config)
{
    Config.MaxNodes = std::max<std::uint32_t>(Config.MaxNodes, 16);
    Config.MaxCorridor = std::max<std::uint32_t>(Config.MaxCorridor, 16);
    Query = dtAllocNavMeshQuery();
    Nodes.resize(Config.MaxNodes);
    const std::uint32_t tableSize = std::bit_ceil(Config.MaxNodes * 2u);
    Table.assign(tableSize, -1);
    TableMask = tableSize - 1;
    Heap.resize(Config.MaxNodes);
    Chain.resize(Config.MaxNodes);
    Corridor.resize(Config.MaxCorridor);
    StraightPath.resize(static_cast<std::size_t>(Config.MaxCorridor) * 3);
    StraightFlags.resize(Config.MaxCorridor);
    StraightRefs.resize(Config.MaxCorridor);
    RaycastPath.resize(Config.MaxCorridor);
}

NavQueryContext::Backend::~Backend()
{
    if (Query != nullptr)
        dtFreeNavMeshQuery(Query);
}

void NavQueryContext::Backend::ResetSearch()
{
    std::fill(Table.begin(), Table.end(), -1);
    NodeCount = 0;
    HeapSize = 0;
}

NavSearchNode* NavQueryContext::Backend::FindNode(std::uint64_t polygon)
{
    for (std::uint32_t slot = Slot(polygon, TableMask); Table[slot] >= 0;
         slot = (slot + 1) & TableMask)
    {
        NavSearchNode& node = Nodes[static_cast<std::uint32_t>(Table[slot])];
        if (node.Polygon == polygon)
            return &node;
    }
    return nullptr;
}

NavSearchNode* NavQueryContext::Backend::AddNode(std::uint64_t polygon)
{
    if (NodeCount >= Nodes.size())
        return nullptr;
    std::uint32_t slot = Slot(polygon, TableMask);
    while (Table[slot] >= 0)
        slot = (slot + 1) & TableMask;
    const std::int32_t index = static_cast<std::int32_t>(NodeCount++);
    Table[slot] = index;
    NavSearchNode& node = Nodes[static_cast<std::uint32_t>(index)];
    node = NavSearchNode{};
    node.Polygon = polygon;
    return &node;
}

void NavQueryContext::Backend::HeapPush(std::int32_t node)
{
    At(node).HeapIndex = static_cast<std::int32_t>(HeapSize);
    Heap[HeapSize++] = node;
    HeapUpdate(node);
}

// Sifts a node toward the root after its estimate decreased.
void NavQueryContext::Backend::HeapUpdate(std::int32_t node)
{
    std::uint32_t i = static_cast<std::uint32_t>(At(node).HeapIndex);
    while (i > 0)
    {
        const std::uint32_t parent = (i - 1) / 2;
        if (!Before(At(node), At(Heap[parent])))
            break;
        Heap[i] = Heap[parent];
        At(Heap[i]).HeapIndex = static_cast<std::int32_t>(i);
        i = parent;
    }
    Heap[i] = node;
    At(node).HeapIndex = static_cast<std::int32_t>(i);
}

std::int32_t NavQueryContext::Backend::HeapPop()
{
    const std::int32_t top = Heap[0];
    At(top).HeapIndex = -1;
    if (--HeapSize == 0)
        return top;
    const std::int32_t last = Heap[HeapSize];
    std::uint32_t i = 0;
    for (std::uint32_t child = 1; child < HeapSize; child = i * 2 + 1)
    {
        if (child + 1 < HeapSize && Before(At(Heap[child + 1]), At(Heap[child])))
            ++child;
        if (!Before(At(Heap[child]), At(last)))
            break;
        Heap[i] = Heap[child];
        At(Heap[i]).HeapIndex = static_cast<std::int32_t>(i);
        i = child;
    }
    Heap[i] = last;
    At(last).HeapIndex = static_cast<std::int32_t>(i);
    return top;
}

NavQueryContext::NavQueryContext(NavQueryContextConfig config)
    : Impl(std::make_unique<Backend>(config))
{
}

NavQueryContext::~NavQueryContext() = default;
NavQueryContext::NavQueryContext(NavQueryContext&&) noexcept = default;
NavQueryContext& NavQueryContext::operator=(NavQueryContext&&) noexcept = default;

const NavQueryContextConfig& NavQueryContext::Config() const { return Impl->Config; }
