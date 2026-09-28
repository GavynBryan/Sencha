#include <math/ResponseCurve.h>

#include <core/json/JsonParser.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
    struct CurveCase
    {
        const char* Name;
        ResponseCurve Curve;
        float Value;
        float Expected;
    };

    ResponseCurve Linear(float low, float high, ResponseCurveDirection direction)
    {
        return ResponseCurve{ .Shape = ResponseCurveShape::Linear, .Direction = direction, .Low = low, .High = high };
    }

    ResponseCurve Step(float threshold, ResponseCurveDirection direction)
    {
        return ResponseCurve{ .Shape = ResponseCurveShape::Step, .Direction = direction, .Low = threshold };
    }

    ResponseCurve Band(float low, float preferredLow, float preferredHigh, float high)
    {
        return ResponseCurve{ .Shape = ResponseCurveShape::Band,
                              .Low = low,
                              .High = high,
                              .PreferredLow = preferredLow,
                              .PreferredHigh = preferredHigh };
    }

    constexpr auto kRising = ResponseCurveDirection::Rising;
    constexpr auto kFalling = ResponseCurveDirection::Falling;
}

TEST(ResponseCurve, EvaluatesEachShapeAgainstAuthoredBounds)
{
    const CurveCase cases[] = {
        { "linear below", Linear(4, 20, kRising), 0.0f, 0.0f },
        { "linear low", Linear(4, 20, kRising), 4.0f, 0.0f },
        { "linear mid", Linear(4, 20, kRising), 12.0f, 0.5f },
        { "linear above", Linear(4, 20, kRising), 40.0f, 1.0f },
        { "linear falling mid", Linear(4, 20, kFalling), 8.0f, 0.75f },
        { "linear falling below", Linear(4, 20, kFalling), 0.0f, 1.0f },
        { "step rising at", Step(3, kRising), 3.0f, 1.0f },
        { "step rising under", Step(3, kRising), 2.9f, 0.0f },
        { "step falling at", Step(3, kFalling), 3.0f, 1.0f },
        { "step falling over", Step(3, kFalling), 3.1f, 0.0f },
        { "band outside low", Band(4, 8, 12, 16), 3.0f, 0.0f },
        { "band ramp in", Band(4, 8, 12, 16), 6.0f, 0.5f },
        { "band preferred", Band(4, 8, 12, 16), 10.0f, 1.0f },
        { "band ramp out", Band(4, 8, 12, 16), 15.0f, 0.25f },
        { "band outside high", Band(4, 8, 12, 16), 17.0f, 0.0f },
        { "band hard edge", Band(8, 8, 12, 12), 8.0f, 1.0f },
    };
    for (const CurveCase& entry : cases)
        EXPECT_FLOAT_EQ(EvaluateResponseCurve(entry.Curve, entry.Value), entry.Expected) << entry.Name;
}

TEST(ResponseCurve, ReadsAuthoredCurveAndRefusesInconsistentBounds)
{
    std::string error;
    const auto band = ReadResponseCurve(
        *JsonParse(R"({"shape":"band","low":4,"preferred_low":8,"preferred_high":12,"high":16})"), error);
    ASSERT_TRUE(band.has_value()) << error;
    EXPECT_EQ(band->Shape, ResponseCurveShape::Band);
    EXPECT_FLOAT_EQ(band->PreferredHigh, 12.0f);

    const auto falling = ReadResponseCurve(*JsonParse(R"({"shape":"linear","direction":"falling","low":0,"high":10})"), error);
    ASSERT_TRUE(falling.has_value()) << error;
    EXPECT_EQ(falling->Direction, ResponseCurveDirection::Falling);

    EXPECT_FALSE(ReadResponseCurve(*JsonParse(R"({"shape":"linear","low":5,"high":5})"), error).has_value());
    EXPECT_FALSE(error.empty());
    error.clear();
    EXPECT_FALSE(ReadResponseCurve(
        *JsonParse(R"({"shape":"band","low":4,"preferred_low":13,"preferred_high":12,"high":16})"), error)
                     .has_value());
    EXPECT_FALSE(error.empty());
}

TEST(ResponseCurve, FieldDescribesEveryAuthoredMember)
{
    const DataFieldSchema field = MakeResponseCurveField("curve", "Curve");
    ASSERT_EQ(field.Kind, DataFieldKind::Record);
    for (const char* key : { "shape", "direction", "low", "high", "preferred_low", "preferred_high" })
        EXPECT_NE(FindChild(field, key), nullptr) << key;
}
