#include <runtime/FramePacer.h>

#include <thread>

void FramePacer::SetTargetFps(double fps)
{
    if (fps <= 0.0)
    {
        Enabled = false;
        TargetFps = 0.0;
        FrameBudget = Clock::duration::zero();
    }
    else
    {
        Enabled = true;
        TargetFps = fps;
        FrameBudget = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(1.0 / fps));
    }
    NextDeadline = Clock::now();
}

void FramePacer::Wait()
{
    if (!Enabled)
        return;

    NextDeadline += FrameBudget;
    const auto now = Clock::now();
    if (NextDeadline < now)
    {
        // Overshot the budget — resync rather than let error accumulate.
        NextDeadline = now;
        return;
    }

    constexpr auto spinMargin = std::chrono::microseconds(500);
    if (NextDeadline - now > spinMargin)
    {
        std::this_thread::sleep_until(NextDeadline - spinMargin);
    }

    while (Clock::now() < NextDeadline)
    {
        // Short spin for sub-millisecond accuracy.
    }
}

void FramePacer::Resync()
{
    NextDeadline = Clock::now();
}

void FramePacer::WaitForLifecycleIdle()
{
    if (Enabled)
    {
        Wait();
        return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Resync();
}
