#pragma once

#include <time/FrameClock.h>

#include <cstdint>

//=============================================================================
// FixedSimulationLoop
//
// Fixed-step simulation clock. RuntimeFrameLoop decides how many fixed ticks a
// frame should run; this type only owns the fixed delta and monotonic tick index.
//
// The main loop drives it:
//
//   TickBudget budget = scheduler.ConsumeTicks(runtimeState);
//   for (uint32_t i = 0; i < budget.TicksToRunThisFrame; ++i)
//       systemHost.RunFixed(sim.BeginFixedTick());
//   systemHost.RunRender(presentation.Alpha);
//=============================================================================
struct TickBudget
{
    uint32_t TicksToRunThisFrame = 0;
};

class FixedSimulationLoop
{
public:
    static constexpr double DefaultFixedDt = 1.0 / 60.0;

    explicit FixedSimulationLoop(double fixedDt = DefaultFixedDt)
        : FixedDt(fixedDt)
    {
    }

    [[nodiscard]] double GetFixedDt() const { return FixedDt; }
    [[nodiscard]] uint64_t GetTickIndex() const { return TickIndex; }

    FixedSimTime BeginFixedTick()
    {
        return FixedSimTime{
            .DeltaSeconds = FixedDt,
            .TickIndex = TickIndex,
        };
    }

    void EndFixedTick()
    {
        ++TickIndex;
    }

    [[nodiscard]] PresentationTime BuildPresentationTime(double alpha) const
    {
        if (alpha < 0.0) alpha = 0.0;
        if (alpha > 1.0) alpha = 1.0;

        return PresentationTime{
            .DeltaSeconds = FixedDt,
            .Alpha = alpha,
        };
    }

    void SetFixedTickRate(double ticksPerSecond)
    {
        if (ticksPerSecond > 0.0)
            FixedDt = 1.0 / ticksPerSecond;
    }

private:
    double FixedDt = DefaultFixedDt;
    uint64_t TickIndex = 0;
};
