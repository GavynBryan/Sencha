#pragma once

#include <time/FrameCadenceLock.h>
#include <time/FrameClock.h>

#include <chrono>
#include <cstdint>
#include <functional>

//=============================================================================
// TimeService
//
// Platform wall-clock source. It owns a steady_clock baseline and produces one
// FrameClock sample per Advance(). The first Advance() after construction
// returns dt = 0 by contract.
//
// The delta the sample carries is the measured delta conditioned by the
// cadence lock: measurement noise around the display's frame period is
// removed before anything converts wall time into simulated time, and the
// unconditioned measurement rides along as MeasuredDt for diagnostics. See
// FrameCadenceLock for why the raw measurement cannot be consumed directly.
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

    [[nodiscard]] FrameCadenceLock& GetCadenceLock() { return CadenceLock; }
    [[nodiscard]] const FrameCadenceLock& GetCadenceLock() const { return CadenceLock; }

private:
    [[nodiscard]] TimePoint Now() const;

    NowSource Source;
    TimePoint LastTime;
    FrameCadenceLock CadenceLock;
    float    ElapsedTime = 0.0f;
    uint64_t FrameIndex = 0;
    bool     FirstFrame = true;
};
