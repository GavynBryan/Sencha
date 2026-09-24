#pragma once

#include <navigation/NavQueryContext.h>

#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

#include <cstdint>
#include <vector>

// Applies a query's forbidden areas inside the Detour calls navigation
// delegates to: projection, nav-raycast, and closest-point.
class NavAreaFilter final : public dtQueryFilter
{
public:
    const bool* Forbidden = nullptr;

    bool passFilter(const dtPolyRef, const dtMeshTile*, const dtPoly* poly) const override
    {
        return Forbidden == nullptr || !Forbidden[poly->getArea()];
    }
};

// One polygon's state in a search.
struct NavSearchNode
{
    static constexpr std::uint32_t kNoLink = 0xffffffffu;

    std::uint64_t Polygon = 0;
    float Position[3] = {};
    // Cost so far, and cost so far plus the heuristic estimate.
    float Cost = 0.0f;
    float Estimate = 0.0f;
    std::int32_t Parent = -1;
    // The link crossed to enter this polygon, or kNoLink for walking in.
    std::uint32_t ViaLink = kNoLink;
    bool ViaReversed = false;
    std::int32_t HeapIndex = -1;
};

// Private to the navigation module: a query context's reusable scratch.
struct NavQueryContext::Backend
{
    NavQueryContextConfig Config;
    dtNavMeshQuery* Query = nullptr;
    NavAreaFilter Filter;

    std::vector<NavSearchNode> Nodes;
    std::uint32_t NodeCount = 0;
    // Open-addressed polygon-to-node table.
    std::vector<std::int32_t> Table;
    std::uint32_t TableMask = 0;
    std::vector<std::int32_t> Heap;
    std::uint32_t HeapSize = 0;

    // Route assembly scratch.
    std::vector<std::int32_t> Chain;
    std::vector<dtPolyRef> Corridor;
    std::vector<float> StraightPath;
    std::vector<unsigned char> StraightFlags;
    std::vector<dtPolyRef> StraightRefs;
    std::vector<dtPolyRef> RaycastPath;

    explicit Backend(const NavQueryContextConfig& config);
    ~Backend();
    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;

    void ResetSearch();
    [[nodiscard]] NavSearchNode* FindNode(std::uint64_t polygon);
    // Null when the node budget is spent.
    [[nodiscard]] NavSearchNode* AddNode(std::uint64_t polygon);
    [[nodiscard]] NavSearchNode& At(std::int32_t index)
    {
        return Nodes[static_cast<std::size_t>(index)];
    }
    [[nodiscard]] std::int32_t IndexOf(const NavSearchNode& node) const
    {
        return static_cast<std::int32_t>(&node - Nodes.data());
    }
    void HeapPush(std::int32_t node);
    void HeapUpdate(std::int32_t node);
    [[nodiscard]] std::int32_t HeapPop();
};
