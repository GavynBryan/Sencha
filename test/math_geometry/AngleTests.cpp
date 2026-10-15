#include <math/Angle.h>

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

static_assert(ToRadians(180.0f) == std::numbers::pi_v<float>);
static_assert(ToRadians(180.0) == std::numbers::pi);
static_assert(ToRadians(-90.0f) == -std::numbers::pi_v<float> / 2.0f);
static_assert(ToDegrees(std::numbers::pi_v<float>) == 180.0f);
static_assert(ToDegrees(std::numbers::pi) == 180.0);
static_assert(ToRadians(0.0f) == 0.0f && ToDegrees(0.0) == 0.0);

// Component defaults written as conversions store the radians their former
// literals did, so scenes that omit those fields load unchanged.
static_assert(ToRadians(60.0f) == 1.0471976f);
static_assert(ToRadians(70.0f) == 1.22173048f);
static_assert(ToRadians(90.0f) == 1.5707964f);

TEST(Angle, ConversionsStayWithinOneUlpOfTheExactValue)
{
    for (int step = -2880; step <= 2880; ++step)
    {
        const float degrees = static_cast<float>(step) * 0.25f;
        const long double exactRadians =
            static_cast<long double>(degrees) * std::numbers::pi_v<long double> / 180.0L;
        const float radians = ToRadians(degrees);
        EXPECT_LE(std::abs(static_cast<long double>(radians) - exactRadians),
                  std::abs(std::nextafter(radians, 2.0f * radians) - radians))
            << degrees << " degrees";

        const long double exactDegrees =
            static_cast<long double>(radians) * 180.0L / std::numbers::pi_v<long double>;
        const float back = ToDegrees(radians);
        EXPECT_LE(std::abs(static_cast<long double>(back) - exactDegrees),
                  std::abs(std::nextafter(back, 2.0f * back) - back))
            << radians << " radians";
    }
}

TEST(Angle, DoublePrecisionRoundTripsWholeDegrees)
{
    for (int degrees = -720; degrees <= 720; ++degrees)
    {
        const double value = static_cast<double>(degrees);
        EXPECT_NEAR(ToDegrees(ToRadians(value)), value, 1e-12) << degrees;
    }
}
