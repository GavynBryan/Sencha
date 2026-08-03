#pragma once

#include <core/json/JsonValue.h>

#include <optional>
#include <string>

struct EngineRuntimeConfig
{
    double FixedTickRate = 60.0;
    double TargetFps = 0.0;
    double ResizeSettleSeconds = 0.10;

    // Catch-up bounds for the fixed-tick accumulator. A frame that spans a long
    // stall (breakpoint, window drag, disk hitch) would otherwise owe more
    // simulation than it can afford, and running it all makes the next frame
    // later still. MaxFixedTicksPerFrame caps the ticks one frame may run and
    // MaxFrameWallDeltaSeconds caps the elapsed time it may claim; the excess
    // is dropped, so simulated time falls behind wall time instead of
    // spiralling. Dropped ticks are reported per frame.
    int MaxFixedTicksPerFrame = 4;
    double MaxFrameWallDeltaSeconds = 0.25;

    // Wall-time budget for async-task commits per frame (the
    // FramePhase::DrainAsyncTasks drain). 0.0 = unbudgeted, matching the
    // TargetFps convention. The first ready commit always runs regardless,
    // so completions can never starve — see AsyncDrainBudget.
    double AsyncCommitBudgetMs = 2.0;

    // Frame-lane job pool size. -1 = auto (hardware_concurrency - 2, the
    // JobSystem default). 0 = single-threaded: every job runs on
    // the calling thread in index order — the engine-wide switch for
    // bisecting threading bugs and for deterministic runs. Positive values
    // pin an explicit worker count.
    int JobWorkerCount = -1;

    // Async-lane task threads (IO, decode, detached zone builds). The default
    // serves room-scale streaming (one room in flight at a time); open-world
    // streaming with several chunks in flight raises it. Must be >= 1: the
    // engine never pumps work inline, so zero threads would strand every load.
    int AsyncTaskThreadCount = 1;

    // World partition streaming policy (WorldPartitionRuntime). HopCount is the
    // neighbor graph distance kept resident around the focus zone; LingerSeconds is
    // how long an undemanded zone stays attached before DestroyZone; ResidentZoneCap
    // bounds the demand set (focus and pins may exceed it; see the policy spec).
    int    StreamingHopCount = 1;
    double StreamingLingerSeconds = 3.0;
    int    StreamingResidentZoneCap = 8;
    // Preloaded (non-focus) zones render and carry static collision so a
    // doorway reads as real space and the threshold crossing lands on resident
    // colliders; logic and audio stay off until entry. Radius adds
    // proximity-based demand from the focus position (0 = graph hops only).
    bool   StreamingNeighborVisible = true;
    bool   StreamingNeighborPhysics = true;
    double StreamingRadius = 0.0;

    // Rebuild the transform propagation order every sweep instead of only when
    // the hierarchy changes. Off by default and never a fast path: it exists so a
    // suspected stale-transform bug can be tested against scoped invalidation in
    // one step, since under-invalidation shows up as a wrong transform rather
    // than a crash.
    bool TransformForceFullPropagation = false;

    bool ExitOnEscape = false;
    bool TogglePauseOnF1 = false;
};

struct RuntimeConfigError
{
    std::string Message;
};

std::optional<EngineRuntimeConfig> DeserializeRuntimeConfig(
    const JsonValue& root,
    RuntimeConfigError* error = nullptr);
