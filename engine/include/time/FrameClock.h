#pragma once

#include <cstdint>

//=============================================================================
// FrameClock
//
// Per-frame wall-clock snapshot produced by TimeService::Advance().
// RuntimeFrameLoop converts Dt into whole fixed ticks, and presentation-rate
// systems read it as FrameUpdateContext::WallDeltaSeconds. No simulation system
// consumes it directly: gameplay that advanced on this clock would run at
// whatever rate frames happen to be presented.
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

// The defaults here mirror FixedSimulationLoop::DefaultFixedDt so a
// default-constructed context carries a sane delta; the scheduling path always
// overwrites them from the configured tick rate.
struct FixedSimTime
{
    double   DeltaSeconds = 1.0 / 60.0;
    uint64_t TickIndex = 0;
};

struct PresentationTime
{
    // Fixed tick delta plus how far this frame sits past the last completed
    // tick, in [0, 1). Renderers that keep per-tick history blend with Alpha;
    // authoritative gameplay must consume FixedSimTime instead.
    double DeltaSeconds = 1.0 / 60.0;
    double Alpha = 0.0;
    uint64_t FrameIndex = 0;
};
