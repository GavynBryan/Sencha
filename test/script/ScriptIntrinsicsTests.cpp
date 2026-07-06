#include "ScriptTestSupport.h"

#include "script/ScriptIntrinsics.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{
    uint64_t Run1(int32_t id, uint64_t a)
    {
        uint64_t window[1] = {a};
        EXPECT_EQ(ExecuteScriptIntrinsic(id, window), ScriptTrapCode::None);
        return window[0];
    }

    uint64_t Run2(int32_t id, uint64_t a, uint64_t b)
    {
        uint64_t window[2] = {a, b};
        EXPECT_EQ(ExecuteScriptIntrinsic(id, window), ScriptTrapCode::None);
        return window[0];
    }

    constexpr int32_t kSqrt = 0;
    constexpr int32_t kSin = 1;
    constexpr int32_t kCos = 2;
    constexpr int32_t kMinF = 4;
    constexpr int32_t kMaxF = 5;
    constexpr int32_t kClampF = 6;
    constexpr int32_t kLerpF = 7;
    constexpr int32_t kAbsI = 8;
    constexpr int32_t kClampI = 11;
    constexpr int32_t kVec3Normalize = 12;
    constexpr int32_t kVec3Cross = 13;
    constexpr int32_t kVec3Distance = 14;
    constexpr int32_t kQuatRotate = 17;
}

TEST(ScriptIntrinsics, NameLookup)
{
    EXPECT_EQ(FindScriptIntrinsic("core.sqrt"), 0);
    EXPECT_EQ(FindScriptIntrinsic("core.sin"), 1);
    EXPECT_EQ(FindScriptIntrinsic("core.quat_rotate_vec3"), 17);
    EXPECT_EQ(FindScriptIntrinsic("physics.raycast"), -1);
    EXPECT_EQ(FindScriptIntrinsic(""), -1);
}

TEST(ScriptIntrinsics, SqrtIsIeeeExact)
{
    EXPECT_EQ(Run1(kSqrt, BitsF(2.0)), 0x3FF6A09E667F3BCDull);
    EXPECT_EQ(Run1(kSqrt, BitsF(9.0)), BitsF(3.0));
    EXPECT_TRUE(std::isnan(FromBitsF(Run1(kSqrt, BitsF(-1.0)))));
}

TEST(ScriptIntrinsics, SinCosGoldenBits)
{
    // Generated from this implementation and frozen: any drift (compiler
    // flags, refactor, platform) fails here. Layout: input, sin, cos.
    struct Golden
    {
        uint64_t X;
        uint64_t Sin;
        uint64_t Cos;
    };
    const Golden grid[] = {
        {BitsF(0.0), 0x0000000000000000ull, 0x3FF0000000000000ull},
        {BitsF(0.5), 0x3FDEAEE8744B05F0ull, 0x3FEC1528065B7D50ull},
        {BitsF(1.0), 0x3FEAED548F090CEEull, 0x3FE14A280FB5068Cull},
        {BitsF(-1.0), 0xBFEAED548F090CEEull, 0x3FE14A280FB5068Cull},
        {BitsF(3.141592653589793), 0x3CA1A62633145C07ull, 0xBFF0000000000000ull},
        {BitsF(1.5707963267948966), 0x3FF0000000000000ull, 0x3C91A62633145C07ull},
        {BitsF(100.0), 0xBFE03425B78C4D9Aull, 0x3FEB981DBF665FF1ull},
        {BitsF(-273.15), 0xBFC579489AF2E588ull, 0xBFEF8BE5883A25E6ull},
        {BitsF(123456.789), 0xBFEFF50E60AB4B39ull, 0x3FAA74D27C4C43CFull},
    };
    for (const Golden& g : grid)
    {
        EXPECT_EQ(Run1(kSin, g.X), g.Sin) << "sin(" << FromBitsF(g.X) << ")";
        EXPECT_EQ(Run1(kCos, g.X), g.Cos) << "cos(" << FromBitsF(g.X) << ")";
    }
}

TEST(ScriptIntrinsics, SinCosAccuracy)
{
    // Gameplay-grade accuracy against the platform libm, which the kernels
    // deliberately do not use.
    for (double x = -50.0; x <= 50.0; x += 0.7)
    {
        EXPECT_NEAR(FromBitsF(Run1(kSin, BitsF(x))), std::sin(x), 1e-9) << x;
        EXPECT_NEAR(FromBitsF(Run1(kCos, BitsF(x))), std::cos(x), 1e-9) << x;
    }
    EXPECT_TRUE(std::isnan(FromBitsF(Run1(kSin, BitsF(std::numeric_limits<double>::infinity())))));
    EXPECT_TRUE(std::isnan(FromBitsF(Run1(kSin, BitsF(std::numeric_limits<double>::quiet_NaN())))));
}

TEST(ScriptIntrinsics, MinMaxPropagateNan)
{
    const uint64_t nan = BitsF(std::numeric_limits<double>::quiet_NaN());
    EXPECT_TRUE(std::isnan(FromBitsF(Run2(kMinF, nan, BitsF(1.0)))));
    EXPECT_TRUE(std::isnan(FromBitsF(Run2(kMinF, BitsF(1.0), nan))));
    EXPECT_TRUE(std::isnan(FromBitsF(Run2(kMaxF, nan, BitsF(1.0)))));
    EXPECT_EQ(FromBitsF(Run2(kMinF, BitsF(1.0), BitsF(2.0))), 1.0);
    EXPECT_EQ(FromBitsF(Run2(kMaxF, BitsF(1.0), BitsF(2.0))), 2.0);
}

TEST(ScriptIntrinsics, ClampTrapsOnInvertedRange)
{
    uint64_t window[3] = {BitsF(0.5), BitsF(2.0), BitsF(1.0)};
    EXPECT_EQ(ExecuteScriptIntrinsic(kClampF, window), ScriptTrapCode::Arg);

    uint64_t good[3] = {BitsF(5.0), BitsF(0.0), BitsF(1.0)};
    EXPECT_EQ(ExecuteScriptIntrinsic(kClampF, good), ScriptTrapCode::None);
    EXPECT_EQ(FromBitsF(good[0]), 1.0);

    uint64_t inverted[3] = {BitsI(0), BitsI(2), BitsI(1)};
    EXPECT_EQ(ExecuteScriptIntrinsic(kClampI, inverted), ScriptTrapCode::Arg);
}

TEST(ScriptIntrinsics, LerpEvaluatesExactForm)
{
    uint64_t window[3] = {BitsF(2.0), BitsF(6.0), BitsF(0.25)};
    EXPECT_EQ(ExecuteScriptIntrinsic(kLerpF, window), ScriptTrapCode::None);
    EXPECT_EQ(FromBitsF(window[0]), 2.0 + (6.0 - 2.0) * 0.25);
}

TEST(ScriptIntrinsics, AbsIWrapsAtIntMin)
{
    EXPECT_EQ(FromBitsI(Run1(kAbsI, BitsI(-5))), 5);
    EXPECT_EQ(FromBitsI(Run1(kAbsI, BitsI(std::numeric_limits<int32_t>::min()))),
              std::numeric_limits<int32_t>::min());
}

TEST(ScriptIntrinsics, Vec3Ops)
{
    uint64_t cross[6] = {BitsF(1.0), BitsF(0.0), BitsF(0.0),
                         BitsF(0.0), BitsF(1.0), BitsF(0.0)};
    EXPECT_EQ(ExecuteScriptIntrinsic(kVec3Cross, cross), ScriptTrapCode::None);
    EXPECT_EQ(FromBitsF(cross[0]), 0.0);
    EXPECT_EQ(FromBitsF(cross[1]), 0.0);
    EXPECT_EQ(FromBitsF(cross[2]), 1.0);

    uint64_t distance[6] = {BitsF(1.0), BitsF(2.0), BitsF(3.0),
                            BitsF(4.0), BitsF(6.0), BitsF(3.0)};
    EXPECT_EQ(ExecuteScriptIntrinsic(kVec3Distance, distance), ScriptTrapCode::None);
    EXPECT_EQ(FromBitsF(distance[0]), 5.0);

    uint64_t normalize[3] = {BitsF(3.0), BitsF(0.0), BitsF(4.0)};
    EXPECT_EQ(ExecuteScriptIntrinsic(kVec3Normalize, normalize), ScriptTrapCode::None);
    EXPECT_EQ(FromBitsF(normalize[0]), 0.6);
    EXPECT_EQ(FromBitsF(normalize[2]), 0.8);

    uint64_t zero[3] = {BitsF(0.0), BitsF(0.0), BitsF(0.0)};
    EXPECT_EQ(ExecuteScriptIntrinsic(kVec3Normalize, zero), ScriptTrapCode::Arg);
}

TEST(ScriptIntrinsics, QuatRotateVec3)
{
    // 90 degrees about +Z takes +X to +Y. Quaternion layout (x, y, z, w).
    const double s = std::sqrt(0.5);
    uint64_t window[7] = {BitsF(0.0), BitsF(0.0), BitsF(s), BitsF(s),
                          BitsF(1.0), BitsF(0.0), BitsF(0.0)};
    EXPECT_EQ(ExecuteScriptIntrinsic(kQuatRotate, window), ScriptTrapCode::None);
    EXPECT_NEAR(FromBitsF(window[0]), 0.0, 1e-12);
    EXPECT_NEAR(FromBitsF(window[1]), 1.0, 1e-12);
    EXPECT_NEAR(FromBitsF(window[2]), 0.0, 1e-12);
}
