#include <time/FrameCadenceLock.h>

#include <algorithm>
#include <cmath>

void FrameCadenceLock::SetTolerance(double fractionOfPeriod)
{
    ToleranceFraction = std::clamp(fractionOfPeriod, 0.0, 0.5);
}

double FrameCadenceLock::Apply(double measuredDt)
{
    if (!Enabled || measuredDt <= 0.0)
        return measuredDt;

    Samples[Head] = measuredDt;
    Head = (Head + 1) % kWindow;
    SampleCount = std::min(SampleCount + 1, kWindow);

    // Not enough history to claim a cadence exists.
    if (SampleCount < kWindow / 2)
        return measuredDt;

    // The median gates robustly -- a few hitches cannot move it -- and the
    // mean of the samples that agree with it refines the estimate. The mean is
    // the true cadence: measurement noise defers a timestamp but never unmakes
    // a frame, so over the window it cancels.
    std::array<double, kWindow> sorted;
    std::copy_n(Samples.begin(), SampleCount, sorted.begin());
    std::nth_element(sorted.begin(), sorted.begin() + SampleCount / 2,
                     sorted.begin() + SampleCount);
    const double median = sorted[SampleCount / 2];

    const double tolerance = median * ToleranceFraction;
    double agreeingSum = 0.0;
    std::size_t agreeing = 0;
    for (std::size_t i = 0; i < SampleCount; ++i)
    {
        if (std::abs(Samples[i] - median) <= tolerance)
        {
            agreeingSum += Samples[i];
            ++agreeing;
        }
    }

    Locked = agreeing * kAgreementDenominator >= SampleCount * kAgreementNumerator;
    if (!Locked)
    {
        // No cadence to speak of: report time as measured and carry nothing,
        // because there is no better estimate to repay against.
        CarriedError = 0.0;
        return measuredDt;
    }

    // The window mean is the cadence, but it wobbles as noisy samples enter
    // and leave, and every wobble goes straight into the output. Track it
    // slowly so the estimate carries the low-frequency truth and none of the
    // sample churn -- unless it has genuinely moved (a monitor change), where
    // creeping there would mis-snap for seconds.
    const double windowMean = agreeingSum / static_cast<double>(agreeing);
    if (Period <= 0.0
        || std::abs(windowMean - Period) > windowMean * ToleranceFraction)
    {
        Period = windowMean;
    }
    else
    {
        Period += kPeriodAdaptRate * (windowMean - Period);
    }

    const double periods = measuredDt / Period;
    const double whole = std::round(periods);
    const bool snappable =
        whole >= 1.0 && whole <= static_cast<double>(kMaxPeriodsPerFrame)
        && std::abs(measuredDt - whole * Period) <= Period * ToleranceFraction;

    // A hitch the cadence cannot explain is real elapsed time; hiding it would
    // stall the simulation behind the world.
    if (!snappable)
        return measuredDt;

    double out = whole * Period;

    // Repay the carried measurement error a small fraction at a time.
    // Measured deltas telescope -- their sum is exactly the platform clock's
    // elapsed time -- so the error stays bounded at one frame's timestamp
    // jitter plus any slow bias in the period estimate; a proportional
    // repayment keeps reported time tracking the clock while moving each
    // frame's output by microseconds, far below the noise this removes.
    CarriedError += measuredDt - out;
    const double repay = CarriedError * kErrorRepayRate;
    out += repay;
    CarriedError -= repay;

    return out;
}
