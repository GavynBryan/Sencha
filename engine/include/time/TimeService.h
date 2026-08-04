#pragma once

#include <time/FrameClock.h>

#include <chrono>
#include <cstdint>
#include <functional>

//=============================================================================
// TimeService
//
// Platform wall-clock source. It owns a steady_clock baseline and produces one
// raw FrameClock sample per Advance(). The first Advance() after construction
// returns dt = 0 by contract.
//
// This service does not clamp, scale, reset, or accumulate gameplay time.
// Tick scheduling reads these samples (RuntimeFrameLoop), but no simulation
// system consumes them.
//=============================================================================
class TimeService
{
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using NowSource = std::function<TimePoint()>;

    TimeService();

    // Advance the platform clock by one frame. Call exactly once per frame.
    FrameClock Advance();

    // Replace the platform clock. Tick scheduling is a function of elapsed wall
    // time, so testing it deterministically means scripting that time rather
    // than sleeping; an empty source restores steady_clock. Set it before the
    // first Advance() so the baseline comes from the same source.
    void SetNowSource(NowSource source);

private:
    [[nodiscard]] TimePoint Now() const;

    NowSource Source;
    TimePoint LastTime;
    float    ElapsedTime = 0.0f;
    uint64_t FrameIndex = 0;
    bool     FirstFrame = true;
};
