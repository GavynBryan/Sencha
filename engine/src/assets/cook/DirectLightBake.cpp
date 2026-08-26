#include <assets/cook/DirectLightBake.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <assets/cook/BakeBvh.h>
#include <assets/static_mesh/MeshGeometry.h>

namespace
{
    float Clamp01(float value)
    {
        return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    }
}

Vec3d EvaluateBakedDirectRadiance(const Vec3d& worldPosition,
                                  const Vec3d& worldNormal,
                                  std::span<const BakeDirectLight> lights,
                                  const BakeBvh& occluders,
                                  const DirectLightBakeParams& params)
{
    const float wrap = std::max(params.DiffuseWrap, 0.0f);
    const Vec3d rayOrigin = worldPosition + worldNormal * params.NormalOffset;

    Vec3d accum(0.0f, 0.0f, 0.0f);
    for (const BakeDirectLight& light : lights)
    {
        const Vec3d toLight = light.Position - worldPosition;
        // Cull on squared distance so an out-of-range light costs no sqrt.
        const float distanceSquared = toLight.SqrMagnitude();
        if (distanceSquared >= light.Range * light.Range)
            continue;
        const float distance = std::sqrt(distanceSquared);
        const Vec3d lightDir = toLight / std::max(distance, 1e-4f);

        const float diffuse = Clamp01(
            (worldNormal.Dot(lightDir) + wrap) / (1.0f + wrap));
        if (diffuse <= 0.0f)
            continue;

        float cone = 1.0f;
        if (light.Kind == BakeLightKind::Spot)
        {
            const float coneCosine = (-lightDir).Dot(light.Direction);
            cone = Clamp01(coneCosine * light.ConeScale + light.ConeOffset);
            cone *= cone;
            if (cone <= 0.0f)
                continue;
        }

        // (d/r)^4 as two multiplies: std::pow with a runtime exponent lowers to
        // exp2(4*log2(x)), a transcendental pair in the innermost bake loop.
        const float rangeRatio = distance / std::max(light.Range, 1e-4f);
        const float ratioSquared = rangeRatio * rangeRatio;
        const float window = Clamp01(1.0f - ratioSquared * ratioSquared);
        const float attenuation =
            cone * (window * window) / (distance * distance + 1e-4f);
        if (attenuation <= 0.0f)
            continue;

        if (occluders.SegmentOccluded(rayOrigin, light.Position))
            continue;

        accum += light.Color * (light.Intensity * attenuation * diffuse);
    }
    return accum;
}

std::uint32_t EncodeBakedDirectRgb9e5(const Vec3d& radiance)
{
    // Shared-exponent packing per the E5B9G9R9 spec: 9-bit mantissas, 5-bit
    // exponent biased by 15, channel value = mantissa * 2^(exp - 15 - 9).
    constexpr int kMantissaBits = 9;
    constexpr int kExpBias = 15;
    constexpr int kMaxBiasedExp = 31;

    const float r = std::clamp(radiance.X, 0.0f, kBakedDirectMax);
    const float g = std::clamp(radiance.Y, 0.0f, kBakedDirectMax);
    const float b = std::clamp(radiance.Z, 0.0f, kBakedDirectMax);
    const float maxComponent = std::max(r, std::max(g, b));
    if (maxComponent <= 0.0f)
        return 0;

    int biasedExp = std::max(0,
        static_cast<int>(std::floor(std::log2(maxComponent))) + 1 + kExpBias);
    biasedExp = std::min(biasedExp, kMaxBiasedExp);
    float scale = std::exp2(
        static_cast<float>(biasedExp - kExpBias - kMantissaBits));
    // Rounding can carry the largest mantissa to 2^9; renormalize once.
    if (std::lround(maxComponent / scale) == (1 << kMantissaBits)
        && biasedExp < kMaxBiasedExp)
    {
        ++biasedExp;
        scale *= 2.0f;
    }

    const auto mantissa = [scale](float value)
    {
        const long m = std::lround(value / scale);
        return static_cast<std::uint32_t>(
            std::min(m, static_cast<long>((1 << kMantissaBits) - 1)));
    };
    return mantissa(r) | (mantissa(g) << kMantissaBits)
        | (mantissa(b) << (2 * kMantissaBits))
        | (static_cast<std::uint32_t>(biasedExp) << (3 * kMantissaBits));
}

