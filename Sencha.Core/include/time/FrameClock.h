#pragma once

#include <cstdint>

//=============================================================================
// FrameClock
//
// Per-frame wall-clock snapshot produced by TimeService::Advance(). Passed to
// frame-lane systems via SystemHost::RunFrame(). Does not drive fixed-step
// simulation — see SimClock for that.
//
// Dt / Elapsed           — scaled by Timescale. Use for gameplay, animation.
// UnscaledDt / Elapsed   — raw wall time. Use for UI, audio, debug overlays.
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
