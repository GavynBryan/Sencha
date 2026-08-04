#include <time/TimeService.h>

#include <utility>

TimeService::TimeService()
    : LastTime(Clock::now())
{
}

void TimeService::SetNowSource(NowSource source)
{
    Source = std::move(source);
    // Rebase so the first sample after the swap measures from the new source
    // rather than across the two clocks.
    LastTime = Now();
}

TimeService::TimePoint TimeService::Now() const
{
    return Source ? Source() : Clock::now();
}

FrameClock TimeService::Advance()
{
    TimePoint now = Now();

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
