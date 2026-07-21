#pragma once

#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <render/Camera.h>
#include <render/PointLightComponent.h>
#include <render/RenderLight.h>
#include <render/ShadowResidency.h>
#include <render/SpotLightComponent.h>
#include <world/transform/TransformComponents.h>

#include <cstdint>
#include <optional>
#include <vector>

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

// Gathers visible point and spot lights from the active partitions of one
// World, ranks them deterministically, and packs the fixed forward-light
// budget. Every packed light that asks for a shadow emits one request into
// its kind's list, in pack order (score descending, stable key ties): the
// residency arbiter's input order.
class LightExtractionSystem
{
public:
    void Extract(
        const World& world,
        const StoragePartitionSet& partitions,
        const CameraRenderData& camera,
        RenderLightSet& lights,
        std::vector<SpotShadowRequest>& shadowRequests,
        std::vector<PointShadowRequest>& pointShadowRequests,
        LightExtractionCounts* counts = nullptr);

    // Test/tool convenience for an isolated World where every live partition
    // is intentionally visible. Runtime rendering must pass
    // FrameZoneView::Visible.
    void Extract(
        const World& world,
        const CameraRenderData& camera,
        RenderLightSet& lights,
        std::vector<SpotShadowRequest>& shadowRequests,
        std::vector<PointShadowRequest>& pointShadowRequests,
        LightExtractionCounts* counts = nullptr);

    // Test/tool convenience for an isolated World where every live partition is
    // intentionally visible. Runtime rendering must pass FrameZoneView::Visible.
    void Extract(const World& world, RenderLightSet& lights);

private:
    const World* LastWorld = nullptr;
    std::optional<Query<Read<WorldTransform>, Read<PointLightComponent>>> PointQuery;
    std::optional<Query<Read<WorldTransform>, Read<SpotLightComponent>>> SpotQuery;
};
