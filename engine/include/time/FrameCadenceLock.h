#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

//=============================================================================
// FrameCadenceLock
//
// Conditions measured frame deltas against the cadence frames actually present
// at.
//
// A vsynced display shows frames on an exact period, but the delta measured
// between loop iterations carries scheduler and compositor noise -- commonly
// a couple of milliseconds against a 16 ms frame. Everything downstream that
// converts wall time into simulated time inherits that noise: the tick
// accumulator runs bursts it does not owe, and the interpolation alpha samples
// poses at times the display never shows, which reads as spatial jitter
// proportional to speed. The measurement is noisy; the cadence underneath it
// is not.
//
// The lock estimates the recurring period from the measurement stream itself
// -- no display query, so a pacer, an odd compositor, or a mid-session monitor
// change all lock the same way. While most recent samples agree with the
// estimate, each delta snaps to the nearest whole number of periods (a
// dropped frame is two periods, not one noisy one). The measurement error is
// carried and repaid a few microseconds per frame, so long-run elapsed time
// still tracks the platform clock. A delta the cadence cannot explain -- a
// load hitch, an alt-tab -- passes through untouched, and a stream with no
// cadence (uncapped, variable refresh) disengages the lock entirely.
//=============================================================================
class FrameCadenceLock
{
public:
    // Conditions one measured delta, in seconds. Call once per frame.
    [[nodiscard]] double Apply(double measuredDt);

    void SetEnabled(bool enabled) { Enabled = enabled; }
    [[nodiscard]] bool IsEnabled() const { return Enabled; }

    // How far a sample may sit from a whole number of periods and still be
    // the cadence, as a fraction of the period.
    void SetTolerance(double fractionOfPeriod);
    [[nodiscard]] double Tolerance() const { return ToleranceFraction; }

    // Whether enough recent samples agree on a period for snapping to be in
    // effect, and the period they agree on. Diagnostic surfaces read these.
    [[nodiscard]] bool IsLocked() const { return Locked; }
    [[nodiscard]] double PeriodSeconds() const { return Period; }

private:
    static constexpr std::size_t kWindow = 64;
    // A stall spanning more periods than this is a hitch to report faithfully,
    // not a run of dropped frames to tidy.
    static constexpr std::int32_t kMaxPeriodsPerFrame = 8;
    // Share of the window that must agree with the median before snapping.
    static constexpr std::size_t kAgreementNumerator = 3;
    static constexpr std::size_t kAgreementDenominator = 4;
    // Share of the carried measurement error repaid per frame. The error is
    // bounded (measured deltas telescope to the platform clock), so a small
    // share keeps elapsed time honest while each frame's output moves by
    // microseconds.
    static constexpr double kErrorRepayRate = 0.005;
    // How fast the period estimate follows the window mean while the two
    // agree. The output snaps to the estimate, so estimate churn is output
    // churn; this keeps it microseconds per frame.
    static constexpr double kPeriodAdaptRate = 0.02;

    bool Enabled = true;
    double ToleranceFraction = 0.25;

    std::array<double, kWindow> Samples{};
    std::size_t SampleCount = 0;
    std::size_t Head = 0;

    bool Locked = false;
    double Period = 0.0;
    // Measured-minus-output time still owed to the platform clock.
    double CarriedError = 0.0;
};
