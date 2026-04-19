#pragma once

#include <time/FrameClock.h>

#include <cstdint>

//=============================================================================
// SimClock
//
// Tick-based simulation clock. Advanced by the main loop's fixed accumulator,
// never by wall time. SimTime() is therefore deterministic and replay-safe.
//
// The main loop drives it:
//
//   accumulator += frameClock.Dt; // or another sanitized, scaled frame delta
//   accumulator  = std::min(accumulator, maxAccumulator);  // spiral-of-death guard
//   int steps = 0;
//   while (accumulator >= sim.FixedDt && steps < maxSubsteps) {
//       systemHost.RunFixed(sim.FixedDt);
//       sim.Tick++;
//       accumulator -= sim.FixedDt;
//       ++steps;
//   }
//   float alpha = accumulator / sim.FixedDt;
//   systemHost.RunRender(alpha);
//=============================================================================
struct SimClock
{
    uint64_t Tick    = 0;
    float    FixedDt = 1.0f / 60.0f;

    double SimTime() const
    {
        return static_cast<double>(Tick) * static_cast<double>(FixedDt);
    }
};

class FixedSimulationLoop
{
public:
    static constexpr double DefaultFixedDt = 1.0 / 60.0;
    static constexpr double DefaultMaxFrameDt = 1.0 / 15.0;
    static constexpr double DefaultMaxPresentationDt = 1.0 / 15.0;

    explicit FixedSimulationLoop(double fixedDt = DefaultFixedDt)
        : FixedDt(fixedDt)
    {
    }

    [[nodiscard]] double GetFixedDt() const { return FixedDt; }
    [[nodiscard]] double GetAccumulator() const { return Accumulator; }
    [[nodiscard]] uint64_t GetTickIndex() const { return TickIndex; }
    [[nodiscard]] int GetSuppressedPresentationFrames() const { return SuppressedPresentationFrames; }
    [[nodiscard]] uint32_t GetMaxTicksPerFrame() const { return MaxTicksPerFrame; }

    [[nodiscard]] double ClampRawDelta(double rawDt) const
    {
        if (rawDt < 0.0) return 0.0;
        return rawDt > MaxFrameDt ? MaxFrameDt : rawDt;
    }

    void AddFrameDelta(double rawDt)
    {
        Accumulator += ClampRawDelta(rawDt);
        if (Accumulator > MaxAccumulator)
            Accumulator = MaxAccumulator;
    }

    void Accumulate(const EngineFrameTime& engineTime)
    {
        if (!engineTime.IsTemporalDiscontinuity)
            AddFrameDelta(engineTime.SanitizedDeltaSeconds);
    }

    [[nodiscard]] bool HasFixedTick() const
    {
        return Accumulator >= FixedDt;
    }

    FixedSimTime BeginFixedTick()
    {
        return FixedSimTime{
            .DeltaSeconds = FixedDt,
            .TickIndex = TickIndex,
        };
    }

    void EndFixedTick()
    {
        if (Accumulator >= FixedDt)
            Accumulator -= FixedDt;
        ++TickIndex;
    }

    [[nodiscard]] PresentationTime BuildPresentationTime(double rawDt)
    {
        double presentationDt = 0.0;
        if (SuppressedPresentationFrames > 0)
        {
            --SuppressedPresentationFrames;
            presentationDt = FixedDt;
        }
        else
        {
            presentationDt = ClampPresentationDelta(rawDt);
        }

        double alpha = FixedDt > 0.0 ? Accumulator / FixedDt : 0.0;
        if (alpha < 0.0) alpha = 0.0;
        if (alpha > 1.0) alpha = 1.0;

        return PresentationTime{
            .DeltaSeconds = presentationDt,
            .Alpha = alpha,
        };
    }

    void ResetAfterDiscontinuity(int suppressedPresentationFrames = 2)
    {
        Accumulator = 0.0;
        SuppressedPresentationFrames = suppressedPresentationFrames;
    }

    void SetMaxFrameDt(double seconds) { MaxFrameDt = seconds; }
    void SetMaxPresentationDt(double seconds) { MaxPresentationDt = seconds; }
    void SetMaxAccumulator(double seconds) { MaxAccumulator = seconds; }
    void SetMaxTicksPerFrame(uint32_t count) { MaxTicksPerFrame = count == 0 ? 1u : count; }
    void SetFixedTickRate(double ticksPerSecond)
    {
        if (ticksPerSecond > 0.0)
            FixedDt = 1.0 / ticksPerSecond;
    }

private:
    [[nodiscard]] double ClampPresentationDelta(double dt) const
    {
        if (dt < 0.0) return 0.0;
        return dt > MaxPresentationDt ? MaxPresentationDt : dt;
    }

    double FixedDt = DefaultFixedDt;
    double MaxFrameDt = DefaultMaxFrameDt;
    double MaxPresentationDt = DefaultMaxPresentationDt;
    double MaxAccumulator = 0.25;
    double Accumulator = 0.0;
    uint64_t TickIndex = 0;
    uint32_t MaxTicksPerFrame = 5;
    int SuppressedPresentationFrames = 0;
};
