#include "presenters/PlotPeaks.h"

#include <algorithm>
#include <cmath>

std::vector<double> PlotPeaks(std::span<const TimelineSample> samples, double start, double end,
                                      std::size_t bars)
{
    std::vector<double> peaks(bars, 0.0);
    double largest = 0.0;
    for (const TimelineSample& sample : samples)
        largest = std::max(largest, std::abs(sample.Value));
    if (largest <= 0.0 || end <= start || bars == 0)
        return peaks;
    for (const TimelineSample& sample : samples)
    {
        const double fraction = (sample.Time - start) / (end - start);
        if (fraction < 0.0 || fraction > 1.0)
            continue;
        const std::size_t bar = std::min(bars - 1, static_cast<std::size_t>(fraction * static_cast<double>(bars)));
        peaks[bar] = std::max(peaks[bar], std::abs(sample.Value) / largest);
    }
    return peaks;
}
