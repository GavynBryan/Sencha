#pragma once

#include <cstdint>

//=============================================================================
// FrameClock
//
// Per-frame wall-clock snapshot produced by TimeService::Advance(). Passed to
// frame-lane systems via SystemHost::RunFrame(). Does not drive fixed-step
// simulation — see SimClock for that.
//
// Dt / Elapsed           — scaled by Timescale. Feed through a fixed-step loop
//                          before authoritative gameplay consumes it.
// UnscaledDt / Elapsed   — raw wall time after the frame clamp. Use for
//                          telemetry, profiling, and visual-only presentation.
// FrameIndex             — monotonically increasing frame counter.
//=============================================================================
struct FrameClock
{
    float    Dt               = 0.0f;
    float    UnscaledDt       = 0.0f;
    float    Elapsed          = 0.0f;
    float    UnscaledElapsed  = 0.0f;
    float    Timescale        = 1.0f;
    uint64_t FrameIndex       = 0;
};

struct RawFrameTime
{
    double DeltaSeconds = 0.0;
};

struct PlatformFrameTime
{
    double RawDeltaSeconds = 0.0;
    double FrameStartSeconds = 0.0;
    uint64_t FrameIndex = 0;
};

struct EngineFrameTime
{
    double SanitizedDeltaSeconds = 0.0;
    bool IsTemporalDiscontinuity = false;
    uint64_t FrameIndex = 0;
};

struct FixedSimTime
{
    double   DeltaSeconds = 1.0 / 60.0;
    uint64_t TickIndex = 0;
};

struct PresentationTime
{
    double DeltaSeconds = 1.0 / 60.0;
    double Alpha = 0.0;
    uint64_t FrameIndex = 0;
};
