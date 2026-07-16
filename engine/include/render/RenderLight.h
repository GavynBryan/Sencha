#pragma once

#include <math/Mat.h>
#include <math/Vec.h>
#include <math/geometry/3d/Transform3d.h>
#include <render/PointLightComponent.h>
#include <render/SpotLightComponent.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

// GPU-side light record. The final four scalars share one std140 slot:
// light type, shadow slot, and spot-cone scale/offset.
enum class GpuLightType : std::uint32_t
{
    Point = 0,
    Spot = 1,
    Directional = 2,
};

struct GpuLight
{
    Vec4 PositionRange;
    Vec4 DirectionCone;
    Vec4 ColorIntensity;
    std::uint32_t Type = 0;
    std::uint32_t ShadowIndex = UINT32_MAX;
    float ConeScale = 0.0f;
    float ConeOffset = 0.0f;
};

static_assert(sizeof(GpuLight) == 64, "GpuLight must match the std140 light record");

inline constexpr std::uint32_t kMaxForwardLights = 64;
inline constexpr std::uint32_t kMaxSpotShadows = 8;
// Smallest quadtree tile tier. ShadowResolutionTier values are literal tile
// sizes, so the tiers are kSpotShadowMinTileExtent up to the atlas extent by
// powers of two.
inline constexpr std::uint32_t kSpotShadowMinTileExtent = 256;
// Per-camera cap on active irradiance probe volumes. Sizes the probe-volume
// binding array in the lighting descriptor set, which stays dummy-filled
// until probe content is uploaded.
inline constexpr std::uint32_t kMaxActiveProbeVolumes = 8;
inline constexpr std::uint32_t kSpotShadowAtlasExtent = 2048;
inline constexpr std::uint32_t kSpotShadowTileExtent = 512;
inline constexpr std::uint32_t kSpotShadowAtlasColumns =
    kSpotShadowAtlasExtent / kSpotShadowTileExtent;
inline constexpr std::uint32_t kSpotShadowGuardTexels = 8;
inline constexpr std::uint32_t kSpotShadowInnerExtent =
    kSpotShadowTileExtent - 2u * kSpotShadowGuardTexels;
inline constexpr float kSpotShadowSoftnessMinTexels = 0.5f;
inline constexpr float kSpotShadowSoftnessMaxTexels = 4.0f;
// The outer tent tap reaches 1.5 * 4 texels. Linear comparison sampling adds
// one texel of footprint, so the guard must exceed seven texels.
inline constexpr std::uint32_t kSpotShadowFilterReachTexels = 7;
static_assert(kSpotShadowFilterReachTexels < kSpotShadowGuardTexels);
static_assert(kSpotShadowAtlasExtent % kSpotShadowTileExtent == 0);
static_assert(kMaxSpotShadows <= kSpotShadowAtlasColumns * kSpotShadowAtlasColumns);

[[nodiscard]] inline Vec4 SpotShadowAtlasScaleBias(std::uint32_t slot)
{
    constexpr float atlas = static_cast<float>(kSpotShadowAtlasExtent);
    const std::uint32_t column = slot % kSpotShadowAtlasColumns;
    const std::uint32_t row = slot / kSpotShadowAtlasColumns;
    const float scale = static_cast<float>(kSpotShadowInnerExtent) / atlas;
    const float biasX = static_cast<float>(
        column * kSpotShadowTileExtent + kSpotShadowGuardTexels) / atlas;
    const float biasY = static_cast<float>(
        row * kSpotShadowTileExtent + kSpotShadowGuardTexels) / atlas;
    return Vec4(scale, scale, biasX, biasY);
}

struct SpotShadowView
{
    Mat4 ViewProjection = Mat4::Identity();
    Vec4 AtlasScaleBias;
    Vec4 SamplingParams;
    std::uint32_t LightIndex = UINT32_MAX;
};

[[nodiscard]] inline Mat4 MakeSpotShadowProjection(
    float outerAngleDegrees, float nearPlane, float farPlane)
{
    constexpr float degreesToRadians = 0.01745329251994329577f;
    const float fov = std::clamp(outerAngleDegrees * 2.0f, 0.02f, 179.8f)
                    * degreesToRadians;
    const float tanHalfFov = std::tan(fov * 0.5f);
    Mat4 result;
    result[0][0] = 1.0f / tanHalfFov;
    result[1][1] = -1.0f / tanHalfFov;
    result[2][2] = farPlane / (nearPlane - farPlane);
    result[2][3] = (farPlane * nearPlane) / (nearPlane - farPlane);
    result[3][2] = -1.0f;
    return result;
}

// Builds the depth-render and sampling record for one shadowed spot light:
// scale-free light view, near plane clamped against the range, world-space
// texel size at the far plane for the receiver offset, and softness clamped
// to the filter's guard-band budget. AtlasScaleBias and LightIndex are
// assigned at grant time.
[[nodiscard]] inline SpotShadowView MakeSpotShadowView(
    const Transform3f& worldTransform,
    const SpotLightComponent& light,
    float globalSoftness)
{
    constexpr float degreesToRadians = 0.01745329251994329577f;
    const float outerAngle = std::clamp(light.OuterAngleDegrees, 0.01f, 89.9f)
                           * degreesToRadians;
    const float nearPlane = std::max(0.05f, light.Range * 0.02f);
    const Transform3f lightTransform(
        worldTransform.Position,
        worldTransform.Rotation,
        Vec3d(1.0f, 1.0f, 1.0f));
    const Mat4 view = lightTransform.ToMat4().AffineInverse();
    const Mat4 projection = MakeSpotShadowProjection(
        light.OuterAngleDegrees, nearPlane, light.Range);

    SpotShadowView shadow;
    shadow.ViewProjection = projection * view;
    const float texelWorldSize =
        2.0f * light.Range * std::tan(outerAngle)
        / static_cast<float>(kSpotShadowInnerExtent);
    const float softness = std::clamp(
        light.ShadowSoftness * globalSoftness,
        kSpotShadowSoftnessMinTexels,
        kSpotShadowSoftnessMaxTexels);
    shadow.SamplingParams = Vec4(
        texelWorldSize,
        softness,
        std::max(light.ShadowBiasScale, 0.0f),
        0.0f);
    return shadow;
}

[[nodiscard]] inline GpuLight MakePointGpuLight(
    const Vec<3>& worldPosition, const PointLightComponent& light)
{
    GpuLight result;
    result.PositionRange = Vec4(
        worldPosition.X, worldPosition.Y, worldPosition.Z, light.Range);
    result.DirectionCone = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    result.ColorIntensity = Vec4(
        light.Color.X, light.Color.Y, light.Color.Z, light.Intensity);
    result.Type = static_cast<std::uint32_t>(GpuLightType::Point);
    return result;
}

[[nodiscard]] inline GpuLight MakeSpotGpuLight(
    const Vec<3>& worldPosition,
    const Vec<3>& worldDirection,
    const SpotLightComponent& light)
{
    constexpr float degreesToRadians = 0.01745329251994329577f;
    const float outerDegrees = std::clamp(light.OuterAngleDegrees, 0.01f, 89.9f);
    const float innerDegrees = std::clamp(
        light.InnerAngleDegrees, 0.0f, outerDegrees);
    const float cosInner = std::cos(innerDegrees * degreesToRadians);
    const float cosOuter = std::cos(outerDegrees * degreesToRadians);
    const float coneScale = 1.0f / std::max(cosInner - cosOuter, 1.0e-4f);

    GpuLight result;
    result.PositionRange = Vec4(
        worldPosition.X, worldPosition.Y, worldPosition.Z, light.Range);
    result.DirectionCone = Vec4(
        worldDirection.X, worldDirection.Y, worldDirection.Z, cosOuter);
    result.ColorIntensity = Vec4(
        light.Color.X, light.Color.Y, light.Color.Z, light.Intensity);
    result.Type = static_cast<std::uint32_t>(GpuLightType::Spot);
    result.ConeScale = coneScale;
    result.ConeOffset = -cosOuter * coneScale;
    return result;
}

struct RenderLightSet
{
    Vec<3> AmbientSky = Vec<3>(0.10f, 0.12f, 0.15f);
    Vec<3> AmbientGround = Vec<3>(0.04f, 0.03f, 0.02f);

    float DiffuseWrap = 0.25f;
    float MinAmbient = 0.0f;
    float Exposure = 1.0f;
    float TonemapKnee = 0.8f;
    bool TonemapEnabled = true;
    float ShadowDarkness = 1.0f;
    float ShadowSoftness = 1.0f;
    float ShadowBiasConstant = 4.0f;
    float ShadowBiasSlope = 2.0f;

    std::uint32_t Count = 0;
    GpuLight Lights[kMaxForwardLights];
    std::uint32_t SpotShadowCount = 0;
    SpotShadowView SpotShadows[kMaxSpotShadows];

    void Reset()
    {
        Count = 0;
        SpotShadowCount = 0;
    }

    [[nodiscard]] std::uint32_t Add(const GpuLight& light)
    {
        if (Count >= kMaxForwardLights)
            return UINT32_MAX;
        Lights[Count] = light;
        return Count++;
    }

    void AddPoint(const Vec<3>& worldPosition, const PointLightComponent& light)
    {
        (void)Add(MakePointGpuLight(worldPosition, light));
    }

    void AddSpot(const Vec<3>& worldPosition,
                 const Vec<3>& worldDirection,
                 const SpotLightComponent& light)
    {
        (void)Add(MakeSpotGpuLight(worldPosition, worldDirection, light));
    }

    // Grants the next fixed atlas slot to an already-packed light, wiring
    // the slot's inset scale/bias and the light's shadow index. Returns the
    // slot, or UINT32_MAX when the budget is exhausted or the index invalid.
    std::uint32_t GrantSpotShadow(std::uint32_t lightIndex, SpotShadowView view)
    {
        if (lightIndex >= Count || SpotShadowCount >= kMaxSpotShadows)
            return UINT32_MAX;
        const std::uint32_t slot = SpotShadowCount++;
        view.LightIndex = lightIndex;
        view.AtlasScaleBias = SpotShadowAtlasScaleBias(slot);
        SpotShadows[slot] = view;
        Lights[lightIndex].ShadowIndex = slot;
        return slot;
    }
};
