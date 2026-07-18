#include <assets/cook/DirectLightBake.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <assets/cook/BakeBvh.h>
#include <render/static_mesh/MeshGeometry.h>

namespace
{
    float Clamp01(float value)
    {
        return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    }

    std::uint8_t ToUnorm8(float value)
    {
        return static_cast<std::uint8_t>(std::lround(Clamp01(value) * 255.0f));
    }
}

std::uint32_t EncodeBakedDirectRgbm(const Vec3d& radiance)
{
    const float maxComponent =
        std::max(radiance.X, std::max(radiance.Y, radiance.Z));
    if (maxComponent <= 0.0f)
        return 0;

    // Quantize the multiplier up so the divided rgb stays within [0,1].
    float m = Clamp01(maxComponent / kBakedDirectRange);
    m = std::ceil(m * 255.0f) / 255.0f;
    const float inv = 1.0f / (m * kBakedDirectRange);

    const std::uint32_t r = ToUnorm8(radiance.X * inv);
    const std::uint32_t g = ToUnorm8(radiance.Y * inv);
    const std::uint32_t b = ToUnorm8(radiance.Z * inv);
    const std::uint32_t a = ToUnorm8(m);
    // R8G8B8A8_UNORM little-endian: byte 0 = R.
    return r | (g << 8) | (b << 16) | (a << 24);
}

bool BakeDirectLighting(MeshGeometry& geometry,
                        const Mat4& worldTransform,
                        std::span<const BakeDirectLight> lights,
                        const BakeBvh& occluders,
                        const DirectLightBakeParams& params)
{
    const float wrap = std::max(params.DiffuseWrap, 0.0f);
    bool anyLit = false;

    for (StaticMeshVertex& vertex : geometry.Vertices)
    {
        const Vec3d worldPos = worldTransform.TransformPoint(vertex.Position);
        const Vec3d worldNormal =
            worldTransform.TransformVector(vertex.Normal).Normalized();
        const Vec3d rayOrigin = worldPos + worldNormal * params.NormalOffset;

        Vec3d accum(0.0f, 0.0f, 0.0f);
        for (const BakeDirectLight& light : lights)
        {
            const Vec3d toLight = light.Position - worldPos;
            const float distance = toLight.Magnitude();
            if (distance >= light.Range)
                continue;
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

            const float window = Clamp01(
                1.0f - std::pow(distance / std::max(light.Range, 1e-4f), 4.0f));
            const float attenuation =
                cone * (window * window) / (distance * distance + 1e-4f);
            if (attenuation <= 0.0f)
                continue;

            if (occluders.SegmentOccluded(rayOrigin, light.Position))
                continue;

            accum += light.Color * (light.Intensity * attenuation * diffuse);
        }

        vertex.BakedDirect = EncodeBakedDirectRgbm(accum);
        anyLit = anyLit || vertex.BakedDirect != 0;
    }

    return anyLit;
}
