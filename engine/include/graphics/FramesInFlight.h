#pragma once

#include <cstdint>

//=============================================================================
// Frames in flight
//
// One authority for how many frames the CPU may run ahead of the GPU. Every
// per-frame resource -- command pools, fences, scratch slices, timestamp
// pools, offscreen target slots, descriptor update rings -- sizes itself from
// the resolved value, so a subsystem must never carry its own private maximum.
// A fixed-size array indexed by a frame slot is the shape that goes wrong:
// when the configured count exceeds the array, the index either overruns or
// silently folds onto slot 0 and two frames in flight share one resource.
//
// The upper bound is a correctness boundary, not a tuning knob. Four is
// already past the point where added latency stops buying throughput, and it
// bounds the fixed cost of every per-frame pool in the engine.
//
// Vulkan-free on purpose: the editor's target slots need this bound too, and
// the clamp is worth testing without a device.
//=============================================================================

inline constexpr std::uint32_t kMinFramesInFlight = 1;
inline constexpr std::uint32_t kMaxFramesInFlight = 4;

// Folds any configured value into the supported range. Zero means "unset" in
// config and resolves to the minimum rather than disabling rendering.
[[nodiscard]] constexpr std::uint32_t ClampFramesInFlight(std::uint32_t requested)
{
    if (requested < kMinFramesInFlight) return kMinFramesInFlight;
    if (requested > kMaxFramesInFlight) return kMaxFramesInFlight;
    return requested;
}

// What a swapchain's image count answers to besides the surface's limits.
struct SwapchainImageDemand
{
    std::uint32_t FramesInFlight = 1;
    // A display queue limit holds this chain's frames, so its images bound
    // throughput rather than how long a frame waits for the display.
    bool DisplayQueueLimited = false;
};

// `minImages + framesInFlight - 1` lets the application own one image per
// frame in flight while the presentation engine holds `minImages - 1`. Where
// nothing else bounds the display queue that is all a chain gets; elsewhere it
// gets one more (docs/renderer/frame.md, Image count). `maxImages` 0 is unbounded.
[[nodiscard]] constexpr std::uint32_t SwapchainImageCount(std::uint32_t minImages, std::uint32_t maxImages,
                                                          std::uint32_t framesInFlight, bool imagesBoundTheQueue)
{
    constexpr std::uint32_t kImagesBeyondTheFewest = 1;
    const std::uint32_t fewest = minImages + ClampFramesInFlight(framesInFlight) - 1;
    const std::uint32_t wanted = imagesBoundTheQueue ? fewest : fewest + kImagesBeyondTheFewest;
    return maxImages != 0 && wanted > maxImages ? maxImages : wanted;
}
