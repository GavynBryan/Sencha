#pragma once

#include <graphics/GpuFrameRetirement.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

//=============================================================================
// RetiredSlotQueue
//
// Slot indices released back to an allocator, held until the GPU is proven
// done with the frames that could still name them. Deferring the destruction
// of a resource protects its memory; it does nothing for the identity that
// pointed at it, because a submitted command buffer resolves a descriptor
// index at execution time, not at record time. Handing an index straight back
// out means an in-flight frame can sample whatever the next registration put
// there.
//
// Holds no graphics objects, so the recycling policy is testable without a
// device -- which the cache that owns the real descriptors is not.
//=============================================================================
class RetiredSlotQueue
{
public:
    // Release `slot`, stamped with the frame it stopped being used in.
    void Push(std::uint32_t slot, std::uint64_t stamp)
    {
        Entries.push_back(Entry{ slot, stamp });
    }

    // The oldest slot the GPU is proven done with, or nothing when the front
    // of the queue is still in flight. FIFO: the oldest release is also the
    // first to retire, so a later entry can never be ready while the front is
    // not.
    [[nodiscard]] std::optional<std::uint32_t> TryPop(GpuFrameRetirement retirement)
    {
        if (Entries.empty() || !retirement.IsRetired(Entries.front().Stamp))
            return std::nullopt;
        const std::uint32_t slot = Entries.front().Slot;
        Entries.erase(Entries.begin());
        return slot;
    }

    // Released slots not yet proven safe to reuse. Diagnostics: a capacity
    // failure with a nonzero count here is a churn problem, not a leak.
    [[nodiscard]] std::size_t PendingCount() const { return Entries.size(); }

    void Clear() { Entries.clear(); }

private:
    struct Entry
    {
        std::uint32_t Slot = 0;
        std::uint64_t Stamp = 0;
    };

    std::vector<Entry> Entries;
};
