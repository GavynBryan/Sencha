#include "presenters/PlotPeaks.h"
#include "shell/TimelineDeck.h"
#include "workspaces/facets/TimelineFacet.h"

#include <gtest/gtest.h>

// A plotted lane is drawn as a fixed number of columns however long the clock
// runs: each column is the largest sample it covers, scaled to the largest.
TEST(PlotPeaks, EachColumnIsTheLargestSampleItCoversScaledToTheLargest)
{
    std::vector<TimelineSample> samples;
    for (int tick = 0; tick < 1000; ++tick)
        samples.push_back({ .Time = tick / 100.0, .Value = tick < 500 ? 1.0 : 4.0 });
    samples.push_back({ .Time = 2.5, .Value = -2.0 });

    const std::vector<double> peaks = PlotPeaks(samples, 0.0, 10.0, 10);
    ASSERT_EQ(peaks.size(), 10u);
    EXPECT_DOUBLE_EQ(peaks[0], 0.25);
    EXPECT_DOUBLE_EQ(peaks[2], 0.5) << "a sample's size, either way";
    EXPECT_DOUBLE_EQ(peaks[9], 1.0);

    EXPECT_EQ(PlotPeaks(samples, 20.0, 30.0, 4), std::vector<double>(4, 0.0)) << "off the clock";
    EXPECT_EQ(PlotPeaks({}, 0.0, 10.0, 3), std::vector<double>(3, 0.0));
}
