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

    double measured = 0.0;
    if (!FirstFrame)
    {
        using DoubleSeconds = std::chrono::duration<double>;
        measured = std::chrono::duration_cast<DoubleSeconds>(now - LastTime).count();
        if (measured < 0.0)
            measured = 0.0;
    }

    const float delta = static_cast<float>(CadenceLock.Apply(measured));

    FirstFrame = false;
    LastTime = now;
    ElapsedTime += delta;
    ++FrameIndex;

    return FrameClock{
        .Dt = delta,
        .UnscaledDt = delta,
        .MeasuredDt = static_cast<float>(measured),
        .Elapsed = ElapsedTime,
        .UnscaledElapsed = ElapsedTime,
        .Timescale = 1.0f,
        .FrameIndex = FrameIndex,
    };
}
