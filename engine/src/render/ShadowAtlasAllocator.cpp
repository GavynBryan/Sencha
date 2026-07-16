#include <render/ShadowAtlasAllocator.h>

namespace
{
    [[nodiscard]] bool IsValidTileSize(std::uint32_t size)
    {
        if (size < kSpotShadowMinTileExtent || size > kSpotShadowAtlasExtent)
            return false;
        return (size & (size - 1)) == 0;
    }

    // De-interleaves a Morton index into grid coordinates. Scanning nodes in
    // Morton order fills an already-split parent's remaining children before
    // opening a fresh parent, so small tiles pack together instead of
    // fragmenting every large-tier node.
    [[nodiscard]] std::uint32_t CompactEvenBits(std::uint32_t value)
    {
        std::uint32_t result = 0;
        for (std::uint32_t bit = 0; bit < 16; ++bit)
            result |= ((value >> (2 * bit)) & 1u) << bit;
        return result;
    }
}

ShadowAtlasAllocator::ShadowAtlasAllocator()
{
    const std::uint32_t levelCount =
        LevelForSize(kSpotShadowMinTileExtent) + 1;
    Levels.resize(levelCount);
    for (std::uint32_t level = 0; level < levelCount; ++level)
        Levels[level].assign(std::size_t{ 1 } << (2 * level), NodeState::Empty);
}

std::uint32_t ShadowAtlasAllocator::LevelForSize(std::uint32_t size)
{
    std::uint32_t level = 0;
    for (std::uint32_t extent = kSpotShadowAtlasExtent; extent > size; extent >>= 1)
        ++level;
    return level;
}

bool ShadowAtlasAllocator::AncestorsAllowAllocation(std::uint32_t level,
                                                    std::uint32_t gridX,
                                                    std::uint32_t gridY) const
{
    std::uint32_t x = gridX;
    std::uint32_t y = gridY;
    for (std::uint32_t ancestor = level; ancestor > 0;)
    {
        --ancestor;
        x >>= 1;
        y >>= 1;
        const std::uint32_t grid = 1u << ancestor;
        if (Levels[ancestor][y * grid + x] == NodeState::Allocated)
            return false;
    }
    return true;
}

ShadowAtlasAllocation ShadowAtlasAllocator::Allocate(std::uint32_t size)
{
    if (!IsValidTileSize(size))
        return {};

    const std::uint32_t level = LevelForSize(size);
    const std::uint32_t grid = 1u << level;
    std::vector<NodeState>& nodes = Levels[level];

    for (std::uint32_t morton = 0; morton < nodes.size(); ++morton)
    {
        const std::uint32_t gridX = CompactEvenBits(morton);
        const std::uint32_t gridY = CompactEvenBits(morton >> 1);
        const std::uint32_t index = gridY * grid + gridX;
        if (nodes[index] != NodeState::Empty)
            continue;
        if (!AncestorsAllowAllocation(level, gridX, gridY))
            continue;

        nodes[index] = NodeState::Allocated;
        std::uint32_t x = gridX;
        std::uint32_t y = gridY;
        for (std::uint32_t ancestor = level; ancestor > 0;)
        {
            --ancestor;
            x >>= 1;
            y >>= 1;
            const std::uint32_t ancestorGrid = 1u << ancestor;
            NodeState& state = Levels[ancestor][y * ancestorGrid + x];
            if (state == NodeState::Empty)
                state = NodeState::Split;
        }

        ++LiveAllocations;
        LiveTexels += size * size;
        return ShadowAtlasAllocation{
            .X = gridX * size,
            .Y = gridY * size,
            .Size = size,
        };
    }
    return {};
}

void ShadowAtlasAllocator::Free(const ShadowAtlasAllocation& allocation)
{
    if (!allocation.IsValid() || !IsValidTileSize(allocation.Size))
        return;

    const std::uint32_t level = LevelForSize(allocation.Size);
    const std::uint32_t grid = 1u << level;
    const std::uint32_t gridX = allocation.X / allocation.Size;
    const std::uint32_t gridY = allocation.Y / allocation.Size;
    if (gridX >= grid || gridY >= grid)
        return;

    NodeState& node = Levels[level][gridY * grid + gridX];
    if (node != NodeState::Allocated)
        return;
    node = NodeState::Empty;
    --LiveAllocations;
    LiveTexels -= allocation.Size * allocation.Size;

    // Collapse Split ancestors whose subtrees emptied out.
    std::uint32_t x = gridX;
    std::uint32_t y = gridY;
    for (std::uint32_t ancestor = level; ancestor > 0;)
    {
        const std::uint32_t childLevel = ancestor;
        --ancestor;
        x >>= 1;
        y >>= 1;

        const std::uint32_t childGrid = 1u << childLevel;
        bool allEmpty = true;
        for (std::uint32_t childY = 2 * y; childY < 2 * y + 2 && allEmpty; ++childY)
        for (std::uint32_t childX = 2 * x; childX < 2 * x + 2 && allEmpty; ++childX)
            allEmpty = Levels[childLevel][childY * childGrid + childX] == NodeState::Empty;
        if (!allEmpty)
            break;

        const std::uint32_t ancestorGrid = 1u << ancestor;
        NodeState& state = Levels[ancestor][y * ancestorGrid + x];
        if (state != NodeState::Split)
            break;
        state = NodeState::Empty;
    }
}

void ShadowAtlasAllocator::Reset()
{
    for (std::vector<NodeState>& nodes : Levels)
        nodes.assign(nodes.size(), NodeState::Empty);
    LiveAllocations = 0;
    LiveTexels = 0;
}

Vec4 ShadowAtlasAllocator::InsetScaleBias(const ShadowAtlasAllocation& allocation)
{
    constexpr float atlas = static_cast<float>(kSpotShadowAtlasExtent);
    const float logical = static_cast<float>(
        allocation.Size - 2u * kSpotShadowGuardTexels);
    return Vec4(logical / atlas,
                logical / atlas,
                static_cast<float>(allocation.X + kSpotShadowGuardTexels) / atlas,
                static_cast<float>(allocation.Y + kSpotShadowGuardTexels) / atlas);
}
