#pragma once

#include <render/LightGpuTypes.h>
#ifdef SENCHA_ENABLE_RENDER_PROFILING
#include <render/RenderDebugView.h>
#endif

// Frame-facing aggregate. GPU record definitions and packing mechanisms live
// in LightGpuTypes so descriptor/upload code can depend on payloads without
// inheriting this mutable frame state.
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
    // Whether the background is drawn from the ambient hemisphere above rather
    // than left as the host's flat clear. Off restores the pre-gradient look in
    // both the game and the editor viewport.
    bool SkyEnabled = true;

    // Projected object shadows (the grounding silhouettes for things that
    // move). The fallback direction is what a caster grounds along when no
    // light dominates it -- the interim authoring source, cvar-fed like the
    // sky colours until an authored environment record replaces it.
    bool ProjectedShadowsEnabled = true;
    float ProjectedShadowDarkness = 0.55f;
    float ProjectedShadowMaxDistance = 6.0f;
    // Where along the projection depth the shadow starts releasing contact,
    // in the silhouette's [0,1] depth space.
    float ProjectedShadowFadeStart = 0.35f;
    float ProjectedShadowSmoothing = 8.0f;
    // Minimum downward pitch of the derived direction, degrees below
    // horizontal; lights below a caster's center cannot push the grounding
    // shadow up or flat.
    float ProjectedShadowMinPitchDegrees = 20.0f;
    Vec<3> ProjectedShadowFallbackDirection = Vec<3>(0.0f, -1.0f, 0.0f);
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

    void AddPoint(const Vec<3>& position, const PointLightComponent& light)
    {
        (void)Add(MakePointGpuLight(position, light));
    }

    void AddSpot(const Vec<3>& position, const Vec<3>& direction,
                 const SpotLightComponent& light)
    {
        (void)Add(MakeSpotGpuLight(position, direction, light));
    }
};
