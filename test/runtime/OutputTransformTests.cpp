// The inverse of the output transform, against the transform as tonemap.glsli
// writes it. A colour tuned on screen that draws inside the scene goes through
// both, so anything short of a round trip shows as a shifted colour.

#include <gtest/gtest.h>

#include <render/OutputTransformParams.h>

#include <algorithm>
#include <cmath>

namespace
{
    // tonemap.glsli's ApplyOutputTransform, one channel.
    float ApplyOutputTransform(const OutputTransformParams& params, float scene)
    {
        float color = scene * std::max(params.Exposure, 0.0f);
        if (!params.TonemapEnabled)
            return std::clamp(color, 0.0f, 1.0f);
        const float knee = std::clamp(params.TonemapKnee, 0.0f, 0.999f);
        if (color <= knee)
            return color;
        const float range = 1.0f - knee;
        const float s = (color - knee) / range;
        return knee + range * s / (1.0f + s);
    }
}

TEST(OutputTransform, SceneValueForDisplayRoundTripsBelowAndAboveTheKnee)
{
    for (const float exposure : { 0.5f, 1.0f, 2.0f })
        for (const float knee : { 0.0f, 0.5f, 0.8f })
            for (const float display : { 0.0f, 0.002f, 0.04f, 0.49f, 0.51f, 0.79f, 0.81f, 0.95f })
            {
                const OutputTransformParams params{ .Exposure = exposure, .TonemapKnee = knee, .TonemapEnabled = true };
                EXPECT_NEAR(ApplyOutputTransform(params, SceneValueForDisplay(params, display)), display, 1e-5f)
                    << "exposure " << exposure << ", knee " << knee;
            }
}

TEST(OutputTransform, WithTheShoulderOffOnlyExposureIsUndone)
{
    const OutputTransformParams params{ .Exposure = 2.0f, .TonemapKnee = 0.8f, .TonemapEnabled = false };
    EXPECT_FLOAT_EQ(SceneValueForDisplay(params, 0.9f), 0.45f);
    EXPECT_FLOAT_EQ(ApplyOutputTransform(params, SceneValueForDisplay(params, 0.9f)), 0.9f);
}

TEST(OutputTransform, ADisplayValueTheShoulderNeverReachesStaysFinite)
{
    const OutputTransformParams params{};
    const float scene = SceneValueForDisplay(params, 1.5f);
    EXPECT_TRUE(std::isfinite(scene));
    EXPECT_GT(ApplyOutputTransform(params, scene), 0.99f);
}
