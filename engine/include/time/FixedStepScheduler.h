#pragma once

#include <cmath>
#include <cstdint>

//=============================================================================
// FixedStepScheduler
//
// Converts elapsed wall time into whole fixed ticks. This is what keeps
// simulation speed independent of how fast frames are presented: a frame
// contributes its wall delta to a residual, and the scheduler emits however
// many whole ticks that residual now covers. Sub-tick remainder carries to the
// next frame, so no time is invented or lost across frames.
//
// The type is deliberately free of clocks, windows, and lifecycle state so the
// conservation invariant (emitted ticks plus residual equals accumulated input)
// is testable on its own. RuntimeFrameLoop owns when it is consulted.
//
// Two bounds keep a stalled or debugger-paused process from trying to catch up
// forever. A single frame's input is clamped, and the ticks emitted from one
// frame are capped; whole ticks beyond the cap are dropped rather than
// deferred, so simulation falls behind wall time instead of spiralling. That
// loss is real and is reported through FixedStepPlan so callers can surface it.
//=============================================================================

struct FixedStepPlan
{
    uint32_t TicksToRun = 0;

    // Residual as a fraction of one fixed tick, in [0, 1). This is how far
    // presentation sits past the last completed tick.
    double Alpha = 0.0;

    // Whole ticks discarded by the per-frame cap. Non-zero means simulated
    // time fell behind wall time this frame.
    uint32_t TicksDropped = 0;

    // Input seconds discarded by the per-frame input clamp.
    double ClampedInputSeconds = 0.0;
};

class FixedStepScheduler
{
public:
    // Accumulate one frame of elapsed simulated time and take the whole ticks
    // it covers. Residual is always left below fixedDt.
    [[nodiscard]] FixedStepPlan Advance(double inputSeconds, double fixedDtSeconds)
    {
        FixedStepPlan plan;
        if (!(fixedDtSeconds > 0.0) || !std::isfinite(fixedDtSeconds))
            return plan;

        double input = std::isfinite(inputSeconds) && inputSeconds > 0.0 ? inputSeconds : 0.0;
        if (input > MaxFrameInputSeconds)
        {
            plan.ClampedInputSeconds = input - MaxFrameInputSeconds;
            input = MaxFrameInputSeconds;
        }

        const double accumulated = ResidualSeconds + input;
        const double wholeTicks = std::floor(accumulated / fixedDtSeconds);

        // The residual drops every whole tick the accumulation covered, including
        // the ones the cap refuses to run. Keeping them would only guarantee the
        // next frame is over budget too.
        ResidualSeconds = accumulated - wholeTicks * fixedDtSeconds;
        if (ResidualSeconds < 0.0)
            ResidualSeconds = 0.0;

        const double cappedTicks = wholeTicks > static_cast<double>(MaxTicksPerFrame)
            ? static_cast<double>(MaxTicksPerFrame)
            : wholeTicks;

        plan.TicksToRun = static_cast<uint32_t>(cappedTicks);
        plan.TicksDropped = static_cast<uint32_t>(wholeTicks - cappedTicks);
        plan.Alpha = GetAlpha(fixedDtSeconds);
        return plan;
    }

    // Drop the residual. Used when simulated time is cut, such as a teleport or
    // a return from a long lifecycle stall, where catching up would replay time
    // that no longer belongs to the new state.
    void Reset() { ResidualSeconds = 0.0; }

    void SetMaxTicksPerFrame(uint32_t ticks) { MaxTicksPerFrame = ticks < 1u ? 1u : ticks; }
    void SetMaxFrameInputSeconds(double seconds)
    {
        if (seconds > 0.0 && std::isfinite(seconds))
            MaxFrameInputSeconds = seconds;
    }

    [[nodiscard]] uint32_t GetMaxTicksPerFrame() const { return MaxTicksPerFrame; }
    [[nodiscard]] double GetMaxFrameInputSeconds() const { return MaxFrameInputSeconds; }
    [[nodiscard]] double GetResidualSeconds() const { return ResidualSeconds; }

    [[nodiscard]] double GetAlpha(double fixedDtSeconds) const
    {
        if (!(fixedDtSeconds > 0.0) || !std::isfinite(fixedDtSeconds))
            return 0.0;
        const double alpha = ResidualSeconds / fixedDtSeconds;
        if (alpha < 0.0) return 0.0;
        if (alpha > 1.0) return 1.0;
        return alpha;
    }

private:
    double ResidualSeconds = 0.0;
    uint32_t MaxTicksPerFrame = 4;
    double MaxFrameInputSeconds = 0.25;
};
