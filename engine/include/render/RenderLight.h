#pragma once

#include <math/Mat.h>
#include <math/Vec.h>
#include <math/geometry/3d/Sphere.h>
#include <math/geometry/3d/Transform3d.h>
#include <math/spatial/GridTransform3d.h>
#include <render/PointLightComponent.h>
#ifdef SENCHA_ENABLE_RENDER_PROFILING
#include <render/RenderDebugView.h>
#endif
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
inline constexpr std::uint32_t kMaxPointShadows = 4;
inline constexpr std::uint32_t kPointShadowFaceCount = 6;
// Every cube face is this size: the cube pool is one fixed image, so faces
// cannot vary per light the way atlas tiles can. Faces need no guard bands;
// hardware cube filtering is seamless across them.
inline constexpr std::uint32_t kPointShadowFaceExtent = 512;
// Smallest quadtree tile tier. ShadowResolutionTier values are literal tile
// sizes, so the tiers are kSpotShadowMinTileExtent up to the atlas extent by
// powers of two.
inline constexpr std::uint32_t kSpotShadowMinTileExtent = 256;
// Per-camera cap on active irradiance probe volumes. Sizes the probe-volume
// binding array in the lighting descriptor set, which stays dummy-filled
// until probe content is uploaded.
inline constexpr std::uint32_t kMaxActiveProbeVolumes = 8;
// SH channel textures per resident volume (R, G, B coefficient volumes); a
// volume slot spans this many consecutive binding-2 array elements.
inline constexpr std::uint32_t kProbeVolumeChannelCount = 3;

// One resident irradiance-probe volume's frame-UBO header: the fragment
// selects the covering volume, maps world position to normalized 3D-texture
// coordinates (uvw = position * scale + bias), and samples its three SH
// channel textures starting at ChannelBase in the binding-2 array. UvwMin/Max
// bound the lattice interior (first to last texel center), so the contains
// test and the sampler clamp agree exactly.
struct GpuProbeVolume
{
    Vec4 ScaleChannelBase;  // xyz world->uvw scale, w first binding-2 element
    Vec4 BiasPriority;      // xyz world->uvw bias, w authored priority
    Vec4 UvwMinVolume;      // xyz lattice min, w world volume (smaller wins)
    Vec4 UvwMaxStableIndex; // xyz lattice max, w cook-order id (final tiebreak)
};

static_assert(sizeof(GpuProbeVolume) == 64,
              "GpuProbeVolume must match the std140 header record");

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
// The reference tier: shadow-view sampling parameters are derived for this
// tile edge and rescaled to the granted allocation's interior at schedule
// time. Also the default request tile size.
inline constexpr std::uint32_t kSpotShadowTileExtent = 512;
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

struct SpotShadowView
{
    Mat4 ViewProjection = Mat4::Identity();
    Vec4 AtlasScaleBias;
    Vec4 SamplingParams;
    std::uint32_t LightIndex = UINT32_MAX;
};

// GPU record for one shadowed point light. Holds the state the cube faces
// were rendered with, not the light's current state, so cached faces are
// always sampled consistently with their contents. The cube array layer is
// the record's own slot index, so it needs no field here.
struct PointShadowView
{
    Vec4 PositionFar;  // rendered light position, far plane (the range)
    Vec4 Params;       // near plane, softness in texels, bias scale, unused
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

// Builds the render and sampling record for one shadowed point light: near
// plane clamped against the range, softness clamped to the same texel budget
// as spot tiles. LightIndex is assigned at grant time. The game extraction
// and the editor's scene gather share this so identical light state produces
// identical records and hashes.
[[nodiscard]] inline PointShadowView MakePointShadowView(
    const Vec<3>& worldPosition,
    const PointLightComponent& light,
    float globalSoftness)
{
    const float nearPlane = std::max(0.05f, light.Range * 0.02f);
    const float softness = std::clamp(
        light.ShadowSoftness * globalSoftness,
        kSpotShadowSoftnessMinTexels,
        kSpotShadowSoftnessMaxTexels);
    PointShadowView shadow;
    shadow.PositionFar = Vec4(
        worldPosition.X, worldPosition.Y, worldPosition.Z, light.Range);
    shadow.Params = Vec4(
        nearPlane, softness, std::max(light.ShadowBiasScale, 0.0f), 0.0f);
    return shadow;
}

// View-projection for rendering one cube face: a 90 degree square frustum
// from the light position down the face axis, in cube face order
// +X -X +Y -Y +Z -Z. The bases follow the cube map texel convention, whose
// T axis already runs down the image, so the projection keeps Y unflipped.
// Relative to the engine's Y-flipping projections that mirrors triangle
// winding: face renders must flip front-face state to compensate.
[[nodiscard]] inline Mat4 MakePointShadowFaceViewProjection(
    const Vec<3>& lightPosition,
    std::uint32_t face,
    float nearPlane,
    float farPlane)
{
    // Right, up, forward triples per face.
    static constexpr float kBases[kPointShadowFaceCount][9] = {
        {  0.0f, 0.0f, -1.0f,   0.0f, -1.0f,  0.0f,    1.0f,  0.0f,  0.0f },
        {  0.0f, 0.0f,  1.0f,   0.0f, -1.0f,  0.0f,   -1.0f,  0.0f,  0.0f },
        {  1.0f, 0.0f,  0.0f,   0.0f,  0.0f,  1.0f,    0.0f,  1.0f,  0.0f },
        {  1.0f, 0.0f,  0.0f,   0.0f,  0.0f, -1.0f,    0.0f, -1.0f,  0.0f },
        {  1.0f, 0.0f,  0.0f,   0.0f, -1.0f,  0.0f,    0.0f,  0.0f,  1.0f },
        { -1.0f, 0.0f,  0.0f,   0.0f, -1.0f,  0.0f,    0.0f,  0.0f, -1.0f },
    };
    const float* basis = kBases[face];

    Mat4 view;
    for (int axis = 0; axis < 3; ++axis)
    {
        view[0][axis] = basis[axis];
        view[1][axis] = basis[3 + axis];
        view[2][axis] = -basis[6 + axis];
    }
    const float position[3] = { lightPosition.X, lightPosition.Y, lightPosition.Z };
    for (int row = 0; row < 3; ++row)
    {
        view[row][3] = -(view[row][0] * position[0]
                         + view[row][1] * position[1]
                         + view[row][2] * position[2]);
    }
    view[3][3] = 1.0f;

    Mat4 projection;
    projection[0][0] = 1.0f;
    projection[1][1] = 1.0f;
    projection[2][2] = farPlane / (nearPlane - farPlane);
    projection[2][3] = (farPlane * nearPlane) / (nearPlane - farPlane);
    projection[3][2] = -1.0f;
    return projection * view;
}

// Ranks a light against a view origin for packing and shadow-slot
// arbitration: intensity scaled by the squared saturated range/distance
// reach, so a light whose volume contains the origin outranks an equally
// bright distant one. The game and the editor must rank identically for the
// editor's budget readout to predict the game's grants.
[[nodiscard]] inline float LightImportanceScore(const Vec<3>& lightPosition,
                                                float range,
                                                float intensity,
                                                const Vec<3>& viewOrigin)
{
    const float distance = Vec<3>::Distance(lightPosition, viewOrigin);
    const float reach = std::clamp(range / std::max(distance, 1.0e-4f), 0.0f, 1.0f);
    return intensity * reach * reach;
}

// Bounding sphere of a spot cone from apex to range cap, used for view-frustum
// culling and for intersecting caster-diff event bounds.
[[nodiscard]] inline Sphere MakeSpotBoundingSphere(const Vec<3>& position,
                                                   const Vec<3>& direction,
                                                   float range,
                                                   float outerAngleDegrees)
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
    bool BakedDirectEnabled = true;
    bool BakedAoEnabled = true;
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    RenderDebugView DebugView = RenderDebugView::None;
#endif

    std::uint32_t Count = 0;
    GpuLight Lights[kMaxForwardLights];
    std::uint32_t SpotShadowCount = 0;
    SpotShadowView SpotShadows[kMaxSpotShadows];
    std::uint32_t PointShadowCount = 0;
    PointShadowView PointShadows[kMaxPointShadows];
    std::uint32_t ProbeVolumeCount = 0;
    GpuProbeVolume ProbeVolumes[kMaxActiveProbeVolumes];

    void Reset()
    {
        Count = 0;
        SpotShadowCount = 0;
        PointShadowCount = 0;
        ProbeVolumeCount = 0;
    }

    bool AddProbeVolume(const GpuProbeVolume& volume)
    {
        if (ProbeVolumeCount >= kMaxActiveProbeVolumes)
            return false;
        ProbeVolumes[ProbeVolumeCount++] = volume;
        return true;
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
