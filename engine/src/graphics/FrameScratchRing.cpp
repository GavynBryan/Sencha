#include <graphics/FrameScratchRing.h>

#include <algorithm>
#include <limits>

namespace
{
    std::uint64_t AlignUp(std::uint64_t value, std::uint64_t alignment)
    {
        if (alignment <= 1) return value;
        if (value > std::numeric_limits<std::uint64_t>::max() - (alignment - 1))
            return std::numeric_limits<std::uint64_t>::max();
        return (value + alignment - 1) & ~(alignment - 1);
    }
}

FrameScratchRing::FrameScratchRing(std::uint32_t framesInFlight, std::uint64_t bytesPerFrame,
                                   const ScratchPoolBudgets& pools)
    : FramesInFlight(framesInFlight == 0 ? 1u : framesInFlight)
    , BytesPerFrame(bytesPerFrame)
    , Pools(pools)
{
    std::uint64_t floors = 0;
    for (const ScratchPoolBudget& pool : Pools)
        floors += pool.Floor;
    if (floors > BytesPerFrame)
    {
        FloorsScaled = true;
        std::uint64_t scaled = 0;
        for (ScratchPoolBudget& pool : Pools)
        {
            pool.Floor = static_cast<std::uint64_t>(
                static_cast<double>(pool.Floor) / static_cast<double>(floors) * static_cast<double>(BytesPerFrame));
            scaled += pool.Floor;
        }
        floors = scaled;
    }
    SharedBytes = BytesPerFrame - floors;
}

FrameScratchRing::SliceSpace FrameScratchRing::SpaceFor(std::uint64_t alignment) const
{
    const std::uint64_t alignedCursor = AlignUp(Cursor, alignment);
    return SliceSpace{
        .AlignedCursor = alignedCursor,
        .Remaining = alignedCursor < BytesPerFrame ? BytesPerFrame - alignedCursor : 0,
    };
}

std::uint64_t FrameScratchRing::PoolRoom(ScratchPool pool, std::uint64_t padding) const
{
    const auto index = static_cast<std::size_t>(pool);
    const ScratchPoolBudget& budget = Pools[index];
    const std::uint64_t used = Used[index];
    const std::uint64_t floorLeft = budget.Floor > used ? budget.Floor - used : 0;
    const std::uint64_t capLeft = budget.Cap > used ? budget.Cap - used : 0;
    const std::uint64_t room = std::min(floorLeft + (SharedBytes - SharedUsed), capLeft);
    return room > padding ? room - padding : 0;
}

FrameScratchRing::Grant FrameScratchRing::Take(std::uint64_t alignedCursor,
                                               std::uint64_t bytes,
                                               ScratchPool pool)
{
    const auto index = static_cast<std::size_t>(pool);
    const std::uint64_t charge = alignedCursor - Cursor + bytes;
    const std::uint64_t floorLeft = Pools[index].Floor > Used[index] ? Pools[index].Floor - Used[index] : 0;
    SharedUsed += charge > floorLeft ? charge - floorLeft : 0;
    Used[index] += charge;
    Cursor = alignedCursor + bytes;
    HighWater = std::max(HighWater, Cursor);
    return Grant{
        .Offset = static_cast<std::uint64_t>(CurrentFrame) * BytesPerFrame + alignedCursor,
        .Bytes = bytes,
    };
}

FrameScratchRing::Grant FrameScratchRing::Allocate(std::uint64_t size,
                                                   std::uint64_t alignment,
                                                   ScratchPool pool)
{
    if (size == 0)
        return {};

    const SliceSpace space = SpaceFor(alignment);
    if (size > std::min(space.Remaining, PoolRoom(pool, space.AlignedCursor - Cursor)))
    {
        ++FailedAllocations;
        return {};
    }
    return Take(space.AlignedCursor, size, pool);
}

FrameScratchRing::Grant FrameScratchRing::AllocateElements(std::uint64_t maxElements,
                                                           std::uint64_t stride,
                                                           std::uint64_t alignment,
                                                           ScratchPool pool)
{
    if (maxElements == 0 || stride == 0)
        return {};

    const SliceSpace space = SpaceFor(alignment);
    const std::uint64_t room = std::min(space.Remaining, PoolRoom(pool, space.AlignedCursor - Cursor));
    const std::uint64_t fits = std::min(room / stride, maxElements);
    if (fits == 0)
    {
        ++FailedAllocations;
        return {};
    }
    return Take(space.AlignedCursor, fits * stride, pool);
}

std::uint64_t FrameScratchRing::Available(ScratchPool pool, std::uint64_t alignment) const
{
    const SliceSpace space = SpaceFor(alignment);
    return std::min(space.Remaining, PoolRoom(pool, space.AlignedCursor - Cursor));
}
