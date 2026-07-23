#pragma once

#include <render/RenderEntityKey.h>
#include <render/RenderLight.h>
#include <render/ShadowResidencyTypes.h>

#include <vector>

struct ForwardLightCandidate
{
    RenderEntityKey Key;
    Vec<3> Position;
    float Range = 0.0f;
    float Intensity = 0.0f;
    GpuLight Light;
    bool WantsSpotShadow = false;
    bool WantsPointShadow = false;
    SpotShadowView SpotShadow;
    PointShadowView PointShadow;
    Sphere ShadowBounds;
    std::uint32_t ShadowTileSize = 0;
    ShadowUpdatePolicy ShadowPolicy = ShadowUpdatePolicy::OnChange;
    // Importance rank against the view, filled by SelectForwardLights so the
    // sort and the shadow-request score read it instead of recomputing it.
    float Score = 0.0f;
};

struct ForwardLightSelectionCounts
{
    std::uint32_t Candidates = 0;
    std::uint32_t Packed = 0;
};

[[nodiscard]] bool IsUsableForwardLight(float intensity, float range);

[[nodiscard]] ForwardLightCandidate MakePointLightCandidate(
    RenderEntityKey key, const Vec<3>& position,
    const PointLightComponent& light, float globalShadowSoftness);
[[nodiscard]] ForwardLightCandidate MakeSpotLightCandidate(
    RenderEntityKey key, const Transform3f& transform,
    const SpotLightComponent& light, float globalShadowSoftness);

// Applies the one forward-light policy used by runtime and editor: importance
// rank, stable entity-key ties, fixed-budget packing, then shadow requests in
// the same order and with the packed light indices.
void SelectForwardLights(
    std::vector<ForwardLightCandidate>& candidates,
    const Vec<3>& viewOrigin,
    RenderLightSet& lights,
    std::vector<SpotShadowRequest>& spotRequests,
    std::vector<PointShadowRequest>& pointRequests,
    ForwardLightSelectionCounts* counts = nullptr);
