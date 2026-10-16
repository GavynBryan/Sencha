#include <runtime/FramePacer.h>

#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <timeapi.h>

namespace
{
    // Windows default timer resolution is ~15.6ms, which makes sleep_until
    // wildly imprecise for frame pacing. Requesting 1ms resolution process-
    // wide brings std::this_thread::sleep_until to <1ms jitter — the pacer
    // can then land on vsync instead of overshooting by 14ms and colliding
    // with the next swapchain acquire.
    struct TimerResolutionScope
    {
        TimerResolutionScope() { timeBeginPeriod(1); }
        ~TimerResolutionScope() { timeEndPeriod(1); }
    };
    static TimerResolutionScope g_TimerResolutionScope;
}
#endif

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

double FramePacer::Wait()
{
    if (!Enabled)
        return 0.0;

    NextDeadline += FrameBudget;
    const auto now = Clock::now();
    if (NextDeadline < now)
    {
        // Overshot the budget — resync rather than let error accumulate.
        NextDeadline = now;
        return 0.0;
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
    return std::chrono::duration<double>(Clock::now() - now).count();
}

void FramePacer::Resync()
{
    NextDeadline = Clock::now();
}

double FramePacer::WaitForLifecycleIdle()
{
    if (Enabled)
        return Wait();

    const auto start = Clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Resync();
    return std::chrono::duration<double>(Clock::now() - start).count();
}
