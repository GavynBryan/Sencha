#include "script/ScriptIntrinsics.h"

#include <bit>
#include <cmath>
#include <cstring>

namespace
{
    enum Intrinsic : int32_t
    {
        Sqrt = 0,
        Sin = 1,
        Cos = 2,
        AbsF = 3,
        MinF = 4,
        MaxF = 5,
        ClampF = 6,
        LerpF = 7,
        AbsI = 8,
        MinI = 9,
        MaxI = 10,
        ClampI = 11,
        Vec3Normalize = 12,
        Vec3Cross = 13,
        Vec3Distance = 14,
        Vec2Length = 15,
        Vec2Normalize = 16,
        QuatRotateVec3 = 17,
    };

    struct IntrinsicName
    {
        std::string_view Name;
        int32_t Id;
    };

    constexpr IntrinsicName kNames[] = {
        {"core.sqrt", Sqrt},
        {"core.sin", Sin},
        {"core.cos", Cos},
        {"core.abs_f", AbsF},
        {"core.min_f", MinF},
        {"core.max_f", MaxF},
        {"core.clamp_f", ClampF},
        {"core.lerp_f", LerpF},
        {"core.abs_i", AbsI},
        {"core.min_i", MinI},
        {"core.max_i", MaxI},
        {"core.clamp_i", ClampI},
        {"core.vec3_normalize", Vec3Normalize},
        {"core.vec3_cross", Vec3Cross},
        {"core.vec3_distance", Vec3Distance},
        {"core.vec2_length", Vec2Length},
        {"core.vec2_normalize", Vec2Normalize},
        {"core.quat_rotate_vec3", QuatRotateVec3},
    };

    [[nodiscard]] double AsF(uint64_t bits) { return std::bit_cast<double>(bits); }
    [[nodiscard]] uint64_t FromF(double v) { return std::bit_cast<uint64_t>(v); }
    [[nodiscard]] int32_t AsI(uint64_t bits) { return static_cast<int32_t>(bits); }
    [[nodiscard]] uint64_t FromI(int32_t v)
    {
        return static_cast<uint64_t>(static_cast<uint32_t>(v));
    }

    [[nodiscard]] bool IsNan(double v) { return v != v; }

    // Spec D: min/max propagate NaN from either operand (std::fmin/fmax
    // would return the non-NaN operand instead).
    [[nodiscard]] double MinD(double a, double b)
    {
        if (IsNan(a) || IsNan(b))
        {
            return std::numeric_limits<double>::quiet_NaN();
        }
        return a < b ? a : b;
    }

    [[nodiscard]] double MaxD(double a, double b)
    {
        if (IsNan(a) || IsNan(b))
        {
            return std::numeric_limits<double>::quiet_NaN();
        }
        return a > b ? a : b;
    }

    //=========================================================================
    // Deterministic sin/cos.
    //
    // Reduction: fold into [-pi, pi] with fmod (exact per IEEE 754), pick the
    // nearest multiple k of pi/2, and subtract k * pi/2 in split-double form.
    // Kernels: the fdlibm polynomial coefficients for |t| <= pi/4, evaluated
    // in a fixed Horner order. Every operation used (+, -, *, fmod, floor)
    // is exactly rounded, so the result is a pure function of the input bits.
    //=========================================================================

    constexpr double kTau = 6.283185307179586476925286766559;
    constexpr double kPi = 3.141592653589793238462643383279;
    constexpr double kPio2Hi = 1.57079632679489655800e+00; // pi/2 high part
    constexpr double kPio2Lo = 6.12323399573676603587e-17; // pi/2 low part

    // fdlibm __kernel_sin coefficients.
    constexpr double S1 = -1.66666666666666324348e-01;
    constexpr double S2 = 8.33333333332248946124e-03;
    constexpr double S3 = -1.98412698298579493134e-04;
    constexpr double S4 = 2.75573137070700676789e-06;
    constexpr double S5 = -2.50507602534068634195e-08;
    constexpr double S6 = 1.58969099521155010221e-10;

    // fdlibm __kernel_cos coefficients.
    constexpr double C1 = 4.16666666666666019037e-02;
    constexpr double C2 = -1.38888888888741095749e-03;
    constexpr double C3 = 2.48015872894767294178e-05;
    constexpr double C4 = -2.75573143513906633035e-07;
    constexpr double C5 = 2.08757232129817482790e-09;
    constexpr double C6 = -1.13596475577881948265e-11;

    [[nodiscard]] double KernelSin(double t)
    {
        const double z = t * t;
        const double r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
        return t + t * (z * (S1 + z * r));
    }

    [[nodiscard]] double KernelCos(double t)
    {
        // cos t = 1 - z/2 + z^2*(C1 + z*C2 + ...): the polynomial tail
        // enters as z*r, not r.
        const double z = t * t;
        const double r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
        return 1.0 - (0.5 * z - z * r);
    }

    struct Reduced
    {
        double T = 0.0; // |T| <= pi/4 + reduction slack
        int Quadrant = 0;
    };

    [[nodiscard]] Reduced ReduceAngle(double x)
    {
        double r = std::fmod(x, kTau);
        if (r > kPi)
        {
            r -= kTau;
        }
        else if (r < -kPi)
        {
            r += kTau;
        }
        const double k = std::floor(r / kPio2Hi + 0.5);
        const int quadrant = static_cast<int>(k) & 3;
        const double t = (r - k * kPio2Hi) - k * kPio2Lo;
        return {t, quadrant};
    }

    [[nodiscard]] double SinD(double x)
    {
        if (IsNan(x) || std::isinf(x))
        {
            return std::numeric_limits<double>::quiet_NaN();
        }
        const Reduced red = ReduceAngle(x);
        switch (red.Quadrant)
        {
        case 0:  return KernelSin(red.T);
        case 1:  return KernelCos(red.T);
        case 2:  return -KernelSin(red.T);
        default: return -KernelCos(red.T);
        }
    }

    [[nodiscard]] double CosD(double x)
    {
        if (IsNan(x) || std::isinf(x))
        {
            return std::numeric_limits<double>::quiet_NaN();
        }
        const Reduced red = ReduceAngle(x);
        switch (red.Quadrant)
        {
        case 0:  return KernelCos(red.T);
        case 1:  return -KernelSin(red.T);
        case 2:  return -KernelCos(red.T);
        default: return KernelSin(red.T);
        }
    }

    // Fixed evaluation order shared by dot/length/distance/normalize so the
    // scalar path and the VDOT3/VLEN3 opcodes agree bit for bit.
    [[nodiscard]] double Dot3(const double* a, const double* b)
    {
        return (a[0] * b[0] + a[1] * b[1]) + a[2] * b[2];
    }
}

int32_t FindScriptIntrinsic(std::string_view dottedName)
{
    for (const IntrinsicName& entry : kNames)
    {
        if (entry.Name == dottedName)
        {
            return entry.Id;
        }
    }
    return -1;
}

ScriptTrapCode ExecuteScriptIntrinsic(int32_t id, uint64_t* window)
{
    switch (static_cast<Intrinsic>(id))
    {
    case Sqrt:
        window[0] = FromF(std::sqrt(AsF(window[0])));
        return ScriptTrapCode::None;
    case Sin:
        window[0] = FromF(SinD(AsF(window[0])));
        return ScriptTrapCode::None;
    case Cos:
        window[0] = FromF(CosD(AsF(window[0])));
        return ScriptTrapCode::None;
    case AbsF:
        window[0] = FromF(std::fabs(AsF(window[0])));
        return ScriptTrapCode::None;
    case MinF:
        window[0] = FromF(MinD(AsF(window[0]), AsF(window[1])));
        return ScriptTrapCode::None;
    case MaxF:
        window[0] = FromF(MaxD(AsF(window[0]), AsF(window[1])));
        return ScriptTrapCode::None;
    case ClampF:
    {
        const double x = AsF(window[0]);
        const double lo = AsF(window[1]);
        const double hi = AsF(window[2]);
        if (lo > hi)
        {
            return ScriptTrapCode::Arg;
        }
        window[0] = FromF(MaxD(lo, MinD(x, hi)));
        return ScriptTrapCode::None;
    }
    case LerpF:
    {
        const double a = AsF(window[0]);
        const double b = AsF(window[1]);
        const double t = AsF(window[2]);
        window[0] = FromF(a + (b - a) * t);
        return ScriptTrapCode::None;
    }
    case AbsI:
    {
        const int32_t v = AsI(window[0]);
        // abs(INT_MIN) wraps to INT_MIN, matching NEGI.
        window[0] = FromI(v < 0 ? static_cast<int32_t>(0u - static_cast<uint32_t>(v)) : v);
        return ScriptTrapCode::None;
    }
    case MinI:
    {
        const int32_t a = AsI(window[0]);
        const int32_t b = AsI(window[1]);
        window[0] = FromI(a < b ? a : b);
        return ScriptTrapCode::None;
    }
    case MaxI:
    {
        const int32_t a = AsI(window[0]);
        const int32_t b = AsI(window[1]);
        window[0] = FromI(a > b ? a : b);
        return ScriptTrapCode::None;
    }
    case ClampI:
    {
        const int32_t x = AsI(window[0]);
        const int32_t lo = AsI(window[1]);
        const int32_t hi = AsI(window[2]);
        if (lo > hi)
        {
            return ScriptTrapCode::Arg;
        }
        window[0] = FromI(x < lo ? lo : (x > hi ? hi : x));
        return ScriptTrapCode::None;
    }
    case Vec3Normalize:
    {
        double v[3] = {AsF(window[0]), AsF(window[1]), AsF(window[2])};
        const double len = std::sqrt(Dot3(v, v));
        if (len == 0.0 || IsNan(len))
        {
            return ScriptTrapCode::Arg;
        }
        window[0] = FromF(v[0] / len);
        window[1] = FromF(v[1] / len);
        window[2] = FromF(v[2] / len);
        return ScriptTrapCode::None;
    }
    case Vec3Cross:
    {
        const double a[3] = {AsF(window[0]), AsF(window[1]), AsF(window[2])};
        const double b[3] = {AsF(window[3]), AsF(window[4]), AsF(window[5])};
        window[0] = FromF(a[1] * b[2] - a[2] * b[1]);
        window[1] = FromF(a[2] * b[0] - a[0] * b[2]);
        window[2] = FromF(a[0] * b[1] - a[1] * b[0]);
        return ScriptTrapCode::None;
    }
    case Vec3Distance:
    {
        const double a[3] = {AsF(window[0]), AsF(window[1]), AsF(window[2])};
        const double b[3] = {AsF(window[3]), AsF(window[4]), AsF(window[5])};
        const double d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
        window[0] = FromF(std::sqrt(Dot3(d, d)));
        return ScriptTrapCode::None;
    }
    case Vec2Length:
    {
        const double x = AsF(window[0]);
        const double y = AsF(window[1]);
        window[0] = FromF(std::sqrt(x * x + y * y));
        return ScriptTrapCode::None;
    }
    case Vec2Normalize:
    {
        const double x = AsF(window[0]);
        const double y = AsF(window[1]);
        const double len = std::sqrt(x * x + y * y);
        if (len == 0.0 || IsNan(len))
        {
            return ScriptTrapCode::Arg;
        }
        window[0] = FromF(x / len);
        window[1] = FromF(y / len);
        return ScriptTrapCode::None;
    }
    case QuatRotateVec3:
    {
        // v' = v + 2w(q x v) + 2(q x (q x v)), quaternion (x, y, z, w).
        const double q[4] = {AsF(window[0]), AsF(window[1]), AsF(window[2]), AsF(window[3])};
        const double v[3] = {AsF(window[4]), AsF(window[5]), AsF(window[6])};
        const double c1[3] = {
            q[1] * v[2] - q[2] * v[1],
            q[2] * v[0] - q[0] * v[2],
            q[0] * v[1] - q[1] * v[0],
        };
        const double c2[3] = {
            q[1] * c1[2] - q[2] * c1[1],
            q[2] * c1[0] - q[0] * c1[2],
            q[0] * c1[1] - q[1] * c1[0],
        };
        window[0] = FromF(v[0] + 2.0 * (q[3] * c1[0] + c2[0]));
        window[1] = FromF(v[1] + 2.0 * (q[3] * c1[1] + c2[1]));
        window[2] = FromF(v[2] + 2.0 * (q[3] * c1[2] + c2[2]));
        return ScriptTrapCode::None;
    }
    }
    return ScriptTrapCode::Arg;
}
