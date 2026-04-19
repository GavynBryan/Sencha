#include <time/TimeService.h>

TimeService::TimeService()
    : LastTime(Clock::now())
{
}

FrameClock TimeService::Advance()
{
    TimePoint now = Clock::now();

    float delta = 0.0f;
    if (!FirstFrame)
    {
        using FloatSeconds = std::chrono::duration<float>;
        delta = std::chrono::duration_cast<FloatSeconds>(now - LastTime).count();
        if (delta < 0.0f)
            delta = 0.0f;
    }

    FirstFrame = false;
    LastTime = now;
    ElapsedTime += delta;
    ++FrameIndex;

    return FrameClock{
        .Dt = delta,
        .UnscaledDt = delta,
        .Elapsed = ElapsedTime,
        .UnscaledElapsed = ElapsedTime,
        .Timescale = 1.0f,
        .FrameIndex = FrameIndex,
    };
}
