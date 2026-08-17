#pragma once

#include <cstdint>

//=============================================================================
// GpuFrameRetirement
//
// The frame clock a caller needs to answer one question: "has the GPU finished
// with the thing I stopped using?"
//
// Frames are numbered monotonically as they begin recording. A frame's number
// moves into the retired range only when its in-flight fence has actually been
// waited on, so this is anchored to the same proof the deferred-destroy ring
// uses -- not to a count of elapsed frames, and not to a constant chosen to be
// "longer than any realistic frames-in-flight."
//
// Usage is two calls. Stamp when the resource stops being used, then test:
//
//     retired.Push_back({ resource, frame.Retirement.Stamp() });
//     ...
//     if (frame.Retirement.IsRetired(entry.Stamp)) free(entry.resource);
//
// The off-by-one lives here rather than at each call site, because getting it
// wrong is invisible: freeing one frame early is a use-after-free that only
// reproduces under load, at the highest configured frames-in-flight.
//=============================================================================
struct GpuFrameRetirement
{
    // Number of the frame currently recording.
    std::uint64_t Current = 0;

    // Every frame numbered below this has had its fence waited on. Zero means
    // nothing has retired yet, which is the correct reading during the first
    // frames-in-flight frames of a session.
    std::uint64_t RetiredThrough = 0;

    // The number to record against a resource being released this frame.
    [[nodiscard]] std::uint64_t Stamp() const { return Current; }

    // True once the GPU is proven done with everything in flight during the
    // stamped frame. Strictly greater-than: a resource stamped on frame N is
    // still in use by frame N until N itself retires.
    [[nodiscard]] bool IsRetired(std::uint64_t stamp) const
    {
        return stamp < RetiredThrough;
    }

    [[nodiscard]] friend bool operator==(GpuFrameRetirement,
                                         GpuFrameRetirement) = default;
};

// Folds a newly proven-complete frame number into the retired range. Waits can
// be skipped (a slot that never submitted) and a caller may prove completion
// out of order, so this never moves the boundary backwards.
[[nodiscard]] constexpr std::uint64_t AdvanceRetiredThrough(
    std::uint64_t retiredThrough, std::uint64_t completedFrameNumber)
{
    const std::uint64_t candidate = completedFrameNumber + 1;
    return candidate > retiredThrough ? candidate : retiredThrough;
}
