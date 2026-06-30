#pragma once

#include <math/Vec.h>
#include <render/PointLightComponent.h>

#include <cstdint>

//=============================================================================
// RenderLight
//
// The GPU-side light record and the per-frame light set the forward pass
// uploads. RenderLightSet is the lighting sibling of RenderQueue: a transient,
// camera-independent gather rebuilt each frame and consumed by MeshForwardPass.
//
// GpuLight is a tagged record (std140, 64 bytes) so spot and directional lights
// later share the same array and the same shader loop (a new Type case), rather
// than a parallel pipeline per light kind. Today only Point is emitted.
// ShadowIndex is reserved for a future shadow atlas; UINT32_MAX means "no
// shadow", which is the only value written now.
//=============================================================================

enum class GpuLightType : std::uint32_t
{
    Point = 0,
    Spot = 1,        // reserved
    Directional = 2, // reserved
};

struct GpuLight
{
    Vec4 PositionRange;  // xyz world position, w range
    Vec4 DirectionCone;  // xyz direction, w cos(outer) - reserved for spot/directional
    Vec4 ColorIntensity; // rgb linear color, w intensity
    std::uint32_t Type = 0;
    std::uint32_t ShadowIndex = UINT32_MAX;
    std::uint32_t Pad0 = 0;
    std::uint32_t Pad1 = 0;
};

static_assert(sizeof(GpuLight) == 64, "GpuLight must match the std140 light record (4x vec4)");

// Forward pass loops every light per fragment, so this is a deliberately modest
// cap. 64 * 64B = 4KB, well under the 16KB guaranteed dynamic-UBO range. A
// clustered/forward+ cull is the path to raise it; not needed yet.
inline constexpr std::uint32_t kMaxForwardLights = 64;

struct RenderLightSet
{
    // Hemispheric ambient (the no-bake indirect stand-in): sky tint above,
    // ground tint below, blended by surface normal in the shader. Defaults are a
    // neutral cool fill; the pipeline overrides them from render.ambient.* cvars.
    Vec<3> AmbientSky    = Vec<3>(0.10f, 0.12f, 0.15f);
    Vec<3> AmbientGround = Vec<3>(0.04f, 0.03f, 0.02f);

    std::uint32_t Count = 0;
    GpuLight Lights[kMaxForwardLights];

    void Reset() { Count = 0; }

    // Packs a point light at worldPos. Drops the light (no-op) once the cap is
    // reached, so a scene over the cap simply loses its furthest-added lights
    // rather than overrunning the array.
    void AddPoint(const Vec<3>& worldPos, const PointLightComponent& light)
    {
        if (Count >= kMaxForwardLights)
            return;

        GpuLight& out = Lights[Count++];
        out.PositionRange = Vec4(worldPos.X, worldPos.Y, worldPos.Z, light.Range);
        out.DirectionCone = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
        out.ColorIntensity = Vec4(light.Color.X, light.Color.Y, light.Color.Z, light.Intensity);
        out.Type = static_cast<std::uint32_t>(GpuLightType::Point);
        out.ShadowIndex = UINT32_MAX;
        out.Pad0 = 0;
        out.Pad1 = 0;
    }
};
