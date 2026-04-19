#pragma once

#include <cstdint>

//=============================================================================
// FrameClock
//
// Per-frame wall-clock snapshot produced by TimeService::Advance(). This is a
// diagnostic platform clock sample only. RuntimeFrameLoop owns tick scheduling,
// and simulation never consumes any field from this struct.
//
// Dt / UnscaledDt        - raw wall delta from the platform clock. They are
//                          equal while this compatibility snapshot exists.
// Elapsed / UnscaledElapsed
//                        - raw wall elapsed time since this clock was created.
//                          They are equal here.
// Timescale              - always 1.0. Simulation pause state lives on
//                          RuntimeFrameLoop.
// FrameIndex             - monotonically increasing platform frame counter.
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

struct FixedSimTime
{
    double   DeltaSeconds = 1.0 / 60.0;
    uint64_t TickIndex = 0;
};

struct PresentationTime
{
    // Fixed presentation tick delta plus render interpolation alpha. Authoritative
    // gameplay must consume FixedSimTime instead.
    double DeltaSeconds = 1.0 / 60.0;
    double Alpha = 0.0;
    uint64_t FrameIndex = 0;
};
