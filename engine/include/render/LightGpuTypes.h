#pragma once

#include <math/Mat.h>
#include <math/Vec.h>
#include <math/geometry/3d/Sphere.h>
#include <math/geometry/3d/Transform3d.h>
#include <math/spatial/GridTransform3d.h>
#include <render/PointLightComponent.h>
#include <render/SpotLightComponent.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

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

static_assert(sizeof(GpuLight) == 64);

inline constexpr std::uint32_t kMaxForwardLights = 64;
inline constexpr std::uint32_t kMaxSpotShadows = 8;
inline constexpr std::uint32_t kMaxPointShadows = 4;
inline constexpr std::uint32_t kPointShadowFaceCount = 6;
inline constexpr std::uint32_t kPointShadowFaceExtent = 512;
inline constexpr std::uint32_t kSpotShadowMinTileExtent = 256;
inline constexpr std::uint32_t kMaxActiveProbeVolumes = 8;
inline constexpr std::uint32_t kProbeVolumeChannelCount = 3;

struct GpuProbeVolume
{
    Vec4 ScaleChannelBase;
    Vec4 BiasPriority;
    Vec4 UvwMinVolume;
    Vec4 UvwMaxStableIndex;
};

static_assert(sizeof(GpuProbeVolume) == 64);

inline GpuProbeVolume MakeGpuProbeVolume(const GridTransform3d& grid,
                                         std::int32_t priority,
                                         std::uint32_t stableIndex,
                                         std::uint32_t slot)
{
    const Vec<3> dims(static_cast<float>(grid.DimsX),
                      static_cast<float>(grid.DimsY),
                      static_cast<float>(grid.DimsZ));
    const Vec<3> scale(1.0f / (grid.CellSize * dims.X),
                       1.0f / (grid.CellSize * dims.Y),
                       1.0f / (grid.CellSize * dims.Z));
    const Vec<3> bias(0.5f / dims.X - grid.Origin.X * scale.X,
                      0.5f / dims.Y - grid.Origin.Y * scale.Y,
                      0.5f / dims.Z - grid.Origin.Z * scale.Z);
    const Vec<3> extent = grid.Bounds().Size();

    GpuProbeVolume volume;
    volume.ScaleChannelBase = Vec4(scale.X, scale.Y, scale.Z,
        static_cast<float>(slot * kProbeVolumeChannelCount));
    volume.BiasPriority = Vec4(bias.X, bias.Y, bias.Z,
        static_cast<float>(priority));
    volume.UvwMinVolume = Vec4(0.5f / dims.X, 0.5f / dims.Y, 0.5f / dims.Z,
        extent.X * extent.Y * extent.Z);
    volume.UvwMaxStableIndex = Vec4(
        (dims.X - 0.5f) / dims.X, (dims.Y - 0.5f) / dims.Y,
        (dims.Z - 0.5f) / dims.Z, static_cast<float>(stableIndex));
    return volume;
}

inline constexpr std::uint32_t kSpotShadowAtlasExtent = 2048;
inline constexpr std::uint32_t kSpotShadowTileExtent = 512;
inline constexpr std::uint32_t kSpotShadowGuardTexels = 8;
inline constexpr std::uint32_t kSpotShadowInnerExtent =
    kSpotShadowTileExtent - 2u * kSpotShadowGuardTexels;
inline constexpr float kSpotShadowSoftnessMinTexels = 0.5f;
inline constexpr float kSpotShadowSoftnessMaxTexels = 4.0f;
inline constexpr std::uint32_t kSpotShadowFilterReachTexels = 7;
static_assert(kSpotShadowFilterReachTexels < kSpotShadowGuardTexels);
static_assert(kSpotShadowAtlasExtent % kSpotShadowTileExtent == 0);

struct SpotShadowView
{
    Mat4 ViewProjection = Mat4::Identity();
    Vec4 AtlasScaleBias;
    Vec4 SamplingParams;
    std::uint32_t LightIndex = UINT32_MAX;
};

struct PointShadowView
{
    Vec4 PositionFar;
    Vec4 Params;
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
        worldTransform.Position, worldTransform.Rotation,
        Vec3d(1.0f, 1.0f, 1.0f));
    const Mat4 view = lightTransform.ToMat4().AffineInverse();
    const Mat4 projection = MakeSpotShadowProjection(
        light.OuterAngleDegrees, nearPlane, light.Range);

    SpotShadowView shadow;
    shadow.ViewProjection = projection * view;
    const float texelWorldSize = 2.0f * light.Range * std::tan(outerAngle)
        / static_cast<float>(kSpotShadowInnerExtent);
    const float softness = std::clamp(
        light.ShadowSoftness * globalSoftness,
        kSpotShadowSoftnessMinTexels, kSpotShadowSoftnessMaxTexels);
    shadow.SamplingParams = Vec4(
        texelWorldSize, softness, std::max(light.ShadowBiasScale, 0.0f), 0.0f);
    return shadow;
}

[[nodiscard]] inline PointShadowView MakePointShadowView(
    const Vec<3>& worldPosition,
    const PointLightComponent& light,
    float globalSoftness)
{
    const float nearPlane = std::max(0.05f, light.Range * 0.02f);
    const float softness = std::clamp(
        light.ShadowSoftness * globalSoftness,
        kSpotShadowSoftnessMinTexels, kSpotShadowSoftnessMaxTexels);
    PointShadowView shadow;
    shadow.PositionFar = Vec4(
        worldPosition.X, worldPosition.Y, worldPosition.Z, light.Range);
    shadow.Params = Vec4(
        nearPlane, softness, std::max(light.ShadowBiasScale, 0.0f), 0.0f);
    return shadow;
}

[[nodiscard]] inline Mat4 MakePointShadowFaceViewProjection(
    const Vec<3>& lightPosition,
    std::uint32_t face,
    float nearPlane,
    float farPlane)
{
    static constexpr float bases[kPointShadowFaceCount][9] = {
        {  0, 0, -1,  0, -1, 0,   1, 0, 0 },
        {  0, 0,  1,  0, -1, 0,  -1, 0, 0 },
        {  1, 0,  0,  0,  0, 1,   0, 1, 0 },
        {  1, 0,  0,  0,  0,-1,   0,-1, 0 },
        {  1, 0,  0,  0, -1, 0,   0, 0, 1 },
        { -1, 0,  0,  0, -1, 0,   0, 0,-1 },
    };
    const float* basis = bases[face];
    Mat4 view;
    for (int axis = 0; axis < 3; ++axis)
    {
        view[0][axis] = basis[axis];
        view[1][axis] = basis[3 + axis];
        view[2][axis] = -basis[6 + axis];
    }
    const float position[3] = {
        lightPosition.X, lightPosition.Y, lightPosition.Z };
    for (int row = 0; row < 3; ++row)
        view[row][3] = -(view[row][0] * position[0]
                       + view[row][1] * position[1]
                       + view[row][2] * position[2]);
    view[3][3] = 1.0f;

    Mat4 projection;
    projection[0][0] = 1.0f;
    projection[1][1] = 1.0f;
    projection[2][2] = farPlane / (nearPlane - farPlane);
    projection[2][3] = (farPlane * nearPlane) / (nearPlane - farPlane);
    projection[3][2] = -1.0f;
    return projection * view;
}

[[nodiscard]] inline float LightImportanceScore(
    const Vec<3>& lightPosition, float range, float intensity,
    const Vec<3>& viewOrigin)
{
    const float distance = Vec<3>::Distance(lightPosition, viewOrigin);
    const float reach = std::clamp(
        range / std::max(distance, 1.0e-4f), 0.0f, 1.0f);
    return intensity * reach * reach;
}

[[nodiscard]] inline Sphere MakeSpotBoundingSphere(
    const Vec<3>& position, const Vec<3>& direction,
    float range, float outerAngleDegrees)
{
    constexpr float degreesToRadians = 0.01745329251994329577f;
    const float angle = std::clamp(outerAngleDegrees, 0.01f, 89.9f)
                      * degreesToRadians;
    const float halfRange = range * 0.5f;
    const float coneRadius = range * std::tan(angle);
    const float radius = std::sqrt(halfRange * halfRange + coneRadius * coneRadius);
    return Sphere(position + direction * halfRange, radius);
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
    const Vec<3>& worldPosition, const Vec<3>& worldDirection,
    const SpotLightComponent& light)
{
    constexpr float degreesToRadians = 0.01745329251994329577f;
    const float outer = std::clamp(light.OuterAngleDegrees, 0.01f, 89.9f);
    const float inner = std::clamp(light.InnerAngleDegrees, 0.0f, outer);
    const float cosInner = std::cos(inner * degreesToRadians);
    const float cosOuter = std::cos(outer * degreesToRadians);
    const float scale = 1.0f / std::max(cosInner - cosOuter, 1.0e-4f);

    GpuLight result;
    result.PositionRange = Vec4(
        worldPosition.X, worldPosition.Y, worldPosition.Z, light.Range);
    result.DirectionCone = Vec4(
        worldDirection.X, worldDirection.Y, worldDirection.Z, cosOuter);
    result.ColorIntensity = Vec4(
        light.Color.X, light.Color.Y, light.Color.Z, light.Intensity);
    result.Type = static_cast<std::uint32_t>(GpuLightType::Spot);
    result.ConeScale = scale;
    result.ConeOffset = -cosOuter * scale;
    return result;
}
