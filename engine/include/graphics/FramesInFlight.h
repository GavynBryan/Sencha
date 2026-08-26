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
