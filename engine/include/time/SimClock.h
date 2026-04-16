#pragma once

#include <cstdint>

//=============================================================================
// SimClock
//
// Tick-based simulation clock. Advanced by the main loop's fixed accumulator,
// never by wall time. SimTime() is therefore deterministic and replay-safe.
//
// The main loop drives it:
//
//   accumulator += frameClock.UnscaledDt;
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
