#pragma once

#include <core/json/JsonValue.h>

#include <optional>
#include <string>

struct EngineRuntimeConfig
{
    double FixedTickRate = 60.0;
    double TargetFps = 0.0;
    double ResizeSettleSeconds = 0.10;

    // Wall-time budget for async-task commits per frame (the
    // FramePhase::DrainAsyncTasks drain). 0.0 = unbudgeted, matching the
    // TargetFps convention. The first ready commit always runs regardless,
    // so completions can never starve — see AsyncDrainBudget.
    double AsyncCommitBudgetMs = 2.0;

    // Frame-lane job pool size. -1 = auto (hardware_concurrency - 2, the
    // ThreadPoolJobSystem default). 0 = single-threaded: every job runs on
    // the calling thread in index order — the engine-wide switch for
    // bisecting threading bugs and for deterministic runs. Positive values
    // pin an explicit worker count.
    int JobWorkerCount = -1;

    // Async-lane task threads (IO, decode, detached zone builds). The default
    // serves room-scale streaming (one room in flight at a time); open-world
    // streaming with several chunks in flight raises it. Must be >= 1: the
    // engine never pumps work inline, so zero threads would strand every load.
    int AsyncTaskThreadCount = 1;

    // Propagate transforms one-zone-per-job in the Simulate phase. Off by
    // default: the primary target streams 2-4 room-sized zones, whose whole
    // span costs less than the pool dispatch floor (parallelization.md,
    // Stage C measurements). Games holding many heavy zones live — open-world
    // streaming — turn this on. Both paths produce bit-identical results.
    bool ZoneParallelPropagation = false;

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
