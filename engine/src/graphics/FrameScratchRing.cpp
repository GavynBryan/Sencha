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

FrameScratchRing::SliceSpace FrameScratchRing::SpaceFor(std::uint64_t alignment) const
{
    const std::uint64_t alignedCursor = AlignUp(Cursor, alignment);
    return SliceSpace{
        .AlignedCursor = alignedCursor,
        .Remaining = alignedCursor < BytesPerFrame ? BytesPerFrame - alignedCursor : 0,
    };
}

FrameScratchRing::Grant FrameScratchRing::Take(std::uint64_t alignedCursor,
                                               std::uint64_t bytes)
{
    Cursor = alignedCursor + bytes;
    HighWater = std::max(HighWater, Cursor);
    return Grant{
        .Offset = static_cast<std::uint64_t>(CurrentFrame) * BytesPerFrame + alignedCursor,
        .Bytes = bytes,
    };
}

FrameScratchRing::Grant FrameScratchRing::Allocate(std::uint64_t size,
                                                   std::uint64_t alignment)
{
    if (size == 0)
        return {};

    const SliceSpace space = SpaceFor(alignment);
    if (size > space.Remaining)
    {
        ++FailedAllocations;
        return {};
    }
    return Take(space.AlignedCursor, size);
}

FrameScratchRing::Grant FrameScratchRing::AllocateElements(std::uint64_t maxElements,
                                                           std::uint64_t stride,
                                                           std::uint64_t alignment)
{
    if (maxElements == 0 || stride == 0)
        return {};

    const SliceSpace space = SpaceFor(alignment);
    const std::uint64_t fits = std::min(space.Remaining / stride, maxElements);
    if (fits == 0)
    {
        ++FailedAllocations;
        return {};
    }
    return Take(space.AlignedCursor, fits * stride);
}
