#pragma once

#include <core/service/IService.h>
#include <time/FrameClock.h>

#include <chrono>
#include <cstdint>

//=============================================================================
// TimeService
//
// Platform wall-clock source. It owns a steady_clock baseline and produces one
// raw FrameClock sample per Advance(). The first Advance() after construction
// returns dt = 0 by contract.
//
// This service does not clamp, scale, reset, or accumulate gameplay time.
//=============================================================================
class TimeService : public IService
{
public:
    TimeService();

    // Advance the platform clock by one frame. Call exactly once per frame.
    FrameClock Advance();

private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    TimePoint LastTime;
    float    ElapsedTime = 0.0f;
    uint64_t FrameIndex = 0;
    bool     FirstFrame = true;
};
