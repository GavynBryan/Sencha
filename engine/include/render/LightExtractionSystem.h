#pragma once

#include <render/Camera.h>
#include <render/RenderLight.h>
#include <render/ShadowResidency.h>

#include <span>
#include <vector>

struct Registry;

// Aggregates the extraction already tracks, exposed for the frame counters.
struct LightExtractionCounts
{
    std::uint32_t FrustumCandidates = 0;
    std::uint32_t Packed = 0;

    [[nodiscard]] std::uint32_t DroppedAtCap() const
    {
        return FrustumCandidates - Packed;
    }
};

// Gathers visible point and spot lights across the active registry set, ranks
// them deterministically, and packs the fixed forward-light budget. Every
// packed light that asks for a shadow emits one request into its kind's list,
// in pack order (score descending, stable key ties): the residency arbiter's
// input order.
class LightExtractionSystem
{
public:
    void Extract(std::span<Registry*> registries,
                 const CameraRenderData& camera,
                 RenderLightSet& lights,
                 std::vector<SpotShadowRequest>& shadowRequests,
                 std::vector<PointShadowRequest>& pointShadowRequests,
                 LightExtractionCounts* counts = nullptr) const;
};
