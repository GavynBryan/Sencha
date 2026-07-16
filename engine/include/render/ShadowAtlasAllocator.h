#pragma once

#include <math/Vec.h>
#include <render/RenderLight.h>

#include <cstdint>
#include <vector>

// One physical atlas tile: a power-of-two square at a texel offset. Size 0 is
// the null allocation.
struct ShadowAtlasAllocation
{
    std::uint32_t X = 0;
    std::uint32_t Y = 0;
    std::uint32_t Size = 0;

    [[nodiscard]] bool IsValid() const { return Size != 0; }
    bool operator==(const ShadowAtlasAllocation&) const = default;
};

//=============================================================================
// ShadowAtlasAllocator
//
// Quadtree allocator over the fixed spot shadow atlas: power-of-two tiles
// from kSpotShadowAtlasExtent down to kSpotShadowMinTileExtent. Allocation is
// first-fit in row-major node order at each level, so identical request
// sequences produce identical placements. Pure CPU bookkeeping: the atlas
// image itself lives in LightBindings, and tier-downgrade policy lives in
// the residency arbiter (this class only satisfies or rejects an exact size).
//=============================================================================
class ShadowAtlasAllocator
{
public:
    ShadowAtlasAllocator();

    // Allocates one size x size tile, or the null allocation when no free
    // node of that exact size remains. Size must be a power-of-two tier
    // within [kSpotShadowMinTileExtent, kSpotShadowAtlasExtent].
    [[nodiscard]] ShadowAtlasAllocation Allocate(std::uint32_t size);
    void Free(const ShadowAtlasAllocation& allocation);
    void Reset();

    [[nodiscard]] std::uint32_t AllocatedCount() const { return LiveAllocations; }
    [[nodiscard]] std::uint32_t AllocatedTexels() const { return LiveTexels; }

    // Maps light-space UV [0,1] onto the tile's logical interior (the
    // physical rect inset by the guard band), as consumed by the shader's
    // atlas addressing.
    [[nodiscard]] static Vec4 InsetScaleBias(const ShadowAtlasAllocation& allocation);

private:
    enum class NodeState : std::uint8_t
    {
        Empty,     // nothing allocated here or below
        Split,     // an allocation lives in some descendant
        Allocated, // an allocation lives exactly here
    };

    [[nodiscard]] static std::uint32_t LevelForSize(std::uint32_t size);
    [[nodiscard]] bool AncestorsAllowAllocation(std::uint32_t level,
                                                std::uint32_t gridX,
                                                std::uint32_t gridY) const;

    // Node states for every level, outermost first: level L holds
    // (2^L)^2 nodes of size kSpotShadowAtlasExtent >> L. A node is Empty
    // only when nothing is allocated at or below it, so Empty alone marks
    // an allocatable subtree.
    std::vector<std::vector<NodeState>> Levels;
    std::uint32_t LiveAllocations = 0;
    std::uint32_t LiveTexels = 0;
};
