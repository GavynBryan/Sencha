#pragma once

#include <chrono>

//=============================================================================
// FramePacer
//
// Frame-rate limiter for present modes that don't throttle us (IMMEDIATE,
// MAILBOX) and for battery-saver caps on FIFO. Uses sleep_until to give
// the CPU back, then spins the final margin so the wake-up deadline is
// hit with sub-millisecond accuracy.
//
// SetTargetFps(0.0) disables pacing and Wait() returns immediately.
// The pacer is oblivious to swapchain: it paces wall-clock frames.
// Lifecycle-only frames still call Wait() so the app never busy-loops
// on a minimized window.
//=============================================================================
class FramePacer
{
public:
    using Clock = std::chrono::steady_clock;

    void SetTargetFps(double fps);
    [[nodiscard]] double GetTargetFps() const { return TargetFps; }

    // Call once per frame AFTER the frame's work is complete. Will sleep
    // then spin until the next frame's deadline.
    void Wait();

    // Reset after a lifecycle stall so we don't try to catch up hundreds
    // of frames of backlog.
    void Resync();

    // Lifecycle-only frames have no present/vsync wait. Idle briefly even when
    // the normal FPS cap is disabled so resize/minimize paths do not spin.
    void WaitForLifecycleIdle();

private:
    Clock::time_point NextDeadline{};
    Clock::duration FrameBudget{};
    double TargetFps = 0.0;
    bool Enabled = false;
};
