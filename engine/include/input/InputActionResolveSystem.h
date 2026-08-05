#pragma once

#include <app/GameContexts.h>
#include <input/InputActionResolve.h>
#include <input/InputBindingCache.h>

#include <cstdint>
#include <vector>

class DataAssetCache;
class Logger;
class LoggingProvider;

//=============================================================================
// InputActionResolveSystem
//
// The one reader of raw device state on the gameplay path. Everything else
// reads InputActionState.
//
// PreSimulate, once per rendered frame: fold the frame's transitions and motion
// into both clocks, settle which contexts are active, and resolve the
// presentation snapshot. Running here rather than in FrameUpdate is what lets
// the simulation see this frame's input on this frame's ticks.
//
// FixedLogic, once per fixed tick: resolve the simulation snapshot and record
// it against the tick index. Contexts are settled for the whole frame before
// the first tick, so a frame that runs three ticks resolves all three against
// one context set.
//=============================================================================
class InputActionResolveSystem
{
public:
    InputActionResolveSystem(DataAssetCache& dataAssets, LoggingProvider& logging);

    void PreSimulate(PreSimulateContext& ctx);
    void FixedLogic(FixedLogicContext& ctx);

private:
    // The bound profile for this world, or null when no profile is configured
    // or nothing could be bound from it.
    const BoundInputProfile* ResolveProfile(World& world);

    // The one place bind diagnostics reach a log and the diagnostic surface.
    // The cache holds them as state, so reporting is driven by the revision
    // rather than by whoever happened to ask first.
    void ReportBindStatus(World& world, const InputBindStatus& status);

    DataAssetCache* DataAssets = nullptr;
    Logger* Log = nullptr;
    std::uint64_t ReportedErrorRevision = 0;

    // Device state as of this frame's platform pump. Captured in PreSimulate so
    // the fixed ticks that follow need no access to the platform's frame at all.
    InputDeviceSnapshot Devices;

    // Per-clock carry-over. Owned by the system rather than the world: nothing
    // outside a resolve pass has any business reading a half-consumed latch.
    InputClockState Presentation;
    InputClockState Simulation;

    // Rebuilt at the frame boundary and reused by every tick of that frame.
    std::vector<std::uint8_t> ActiveMask;
};
