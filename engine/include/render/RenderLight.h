#pragma once

#include <math/Mat.h>
#include <math/Vec.h>
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
};
