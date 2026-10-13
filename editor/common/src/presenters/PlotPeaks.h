#pragma once

#include "workspaces/facets/TimelineFacet.h"

#include <cstddef>
#include <span>
#include <vector>

// Samples as `bars` columns from `start` to `end`: each the largest sample in
// its stretch, as a share of the largest of all.
[[nodiscard]] std::vector<double> PlotPeaks(std::span<const TimelineSample> samples, double start, double end,
                                            std::size_t bars);
