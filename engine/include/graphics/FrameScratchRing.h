#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

// Who an allocation is charged to. Each pool's floor is guaranteed to it in any
// allocation order; what floors leave over is shared, first come first served.
enum class ScratchPool : std::uint8_t
{
    MainView,
    World,
    Ui,
    Skinning,
    Optional,
    Count,
};

struct ScratchPoolBudget
{
    std::uint64_t Floor = 0;
    std::uint64_t Cap = std::numeric_limits<std::uint64_t>::max();
};

inline constexpr std::size_t kScratchPoolCount = static_cast<std::size_t>(ScratchPool::Count);
using ScratchPoolBudgets = std::array<ScratchPoolBudget, kScratchPoolCount>;

// The largest offset alignment a descriptor binding into the ring can demand.
// Vulkan caps both minUniformBufferOffsetAlignment and
// minStorageBufferOffsetAlignment at 256 bytes, so no conformant device asks
// for more.
inline constexpr std::uint64_t kMaxDescriptorOffsetAlignment = 256;

// The slice size a ring must use for every slice boundary to be a legal
// descriptor offset. The ring aligns its cursor relative to the slice, so an
// allocation's absolute offset inherits the slice base's alignment: a slice
// size that is not a multiple of the binding alignment leaves every slice
// after the first serving aligned-looking offsets that are absolutely
// misaligned.
[[nodiscard]] constexpr std::uint64_t ResolveScratchSliceBytes(
    std::uint64_t bytesPerFrame, std::uint64_t uniformAlignment)
{
    const std::uint64_t alignment = uniformAlignment > kMaxDescriptorOffsetAlignment
        ? uniformAlignment
        : kMaxDescriptorOffsetAlignment;
    if (alignment == 0)
        return bytesPerFrame;
    return (bytesPerFrame + alignment - 1) / alignment * alignment;
}

//=============================================================================
// FrameScratchRing
//
// The offset arithmetic behind a per-frame scratch allocator: a ring of
// equal-sized slices, one per frame in flight, each carved by a bump cursor
// that resets when its frame comes around again. Holds no memory and no
// graphics objects, so the boundary conditions that decide whether a frame
// renders (exact fit, one byte over, partial grants near the end of a slice)
// are testable without a device.
//
// Offsets returned are absolute within the ring, so a caller adds them to a
// mapped base pointer and to its buffer binding.
//=============================================================================
class FrameScratchRing
{
public:
    // An offset into the ring, or nothing when the slice could not serve the
    // request. Byte counts are what was granted, which for a partial grant is
    // less than what was asked for.
    struct Grant
    {
        std::uint64_t Offset = 0;
        std::uint64_t Bytes = 0;

        [[nodiscard]] bool IsValid() const { return Bytes > 0; }
    };

    FrameScratchRing() = default;
    FrameScratchRing(std::uint32_t framesInFlight, std::uint64_t bytesPerFrame,
                     const ScratchPoolBudgets& pools = {});

    // Rotate to the next slice and reset its cursor and failure count.
    void BeginFrame()
    {
        CurrentFrame = (CurrentFrame + 1) % FramesInFlight;
        Cursor = 0;
        FailedAllocations = 0;
        SharedUsed = 0;
        Used.fill(0);
    }

    // All-or-nothing. For fixed-size uploads whose consumer has no way to use
    // a partial result (a uniform block, a view matrix).
    [[nodiscard]] Grant Allocate(std::uint64_t size, std::uint64_t alignment,
                                 ScratchPool pool = ScratchPool::Optional);

    // As many `stride`-sized elements as the slice can still serve, up to
    // `maxElements`. A caller that can draw in runs uses this so a stream too
    // large for one slice costs it a rebind rather than the whole pass.
    [[nodiscard]] Grant AllocateElements(std::uint64_t maxElements,
                                         std::uint64_t stride,
                                         std::uint64_t alignment,
                                         ScratchPool pool = ScratchPool::Optional);

    // Content bytes `pool` could be granted right now at `alignment`.
    [[nodiscard]] std::uint64_t Available(ScratchPool pool, std::uint64_t alignment) const;

    // True when the configured floors did not fit the slice and were scaled down.
    [[nodiscard]] bool FloorsWereScaled() const { return FloorsScaled; }

    [[nodiscard]] std::uint32_t GetFramesInFlight() const { return FramesInFlight; }
    [[nodiscard]] std::uint64_t GetBytesPerFrame() const { return BytesPerFrame; }
    [[nodiscard]] std::uint64_t GetTotalBytes() const
    {
        return BytesPerFrame * FramesInFlight;
    }
    [[nodiscard]] std::uint64_t GetUsedBytes() const { return Cursor; }
    [[nodiscard]] std::uint64_t GetHighWaterBytes() const { return HighWater; }
    [[nodiscard]] std::uint32_t GetFailedAllocationCount() const
    {
        return FailedAllocations;
    }

private:
    // Where the current slice's cursor sits once aligned, and how much of the
    // slice remains after it. Returns zero remaining when the aligned cursor
    // has already run past the slice.
    struct SliceSpace
    {
        std::uint64_t AlignedCursor = 0;
        std::uint64_t Remaining = 0;
    };
    [[nodiscard]] SliceSpace SpaceFor(std::uint64_t alignment) const;
    [[nodiscard]] std::uint64_t PoolRoom(ScratchPool pool, std::uint64_t padding) const;
    [[nodiscard]] Grant Take(std::uint64_t alignedCursor, std::uint64_t bytes, ScratchPool pool);

    std::uint32_t FramesInFlight = 1;
    std::uint64_t BytesPerFrame = 0;
    std::uint32_t CurrentFrame = 0;
    std::uint64_t Cursor = 0;
    std::uint64_t HighWater = 0;
    std::uint32_t FailedAllocations = 0;
    ScratchPoolBudgets Pools{};
    std::array<std::uint64_t, kScratchPoolCount> Used{};
    std::uint64_t SharedBytes = 0;
    std::uint64_t SharedUsed = 0;
    bool FloorsScaled = false;
};
