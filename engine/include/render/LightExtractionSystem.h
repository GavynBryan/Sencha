#pragma once

#include <render/Camera.h>
#include <render/RenderLight.h>
#include <render/ShadowResidency.h>

#include <span>
#include <vector>

struct Registry;

// Gathers visible point and spot lights across the active registry set, ranks
// them deterministically, and packs the fixed forward-light budget. Every
// packed spot light that asks for a shadow emits one request, in pack order
// (score descending, stable key ties): the residency arbiter's input order.
class LightExtractionSystem
{
public:
    void Extract(std::span<Registry*> registries,
                 const CameraRenderData& camera,
                 RenderLightSet& lights,
                 std::vector<SpotShadowRequest>& shadowRequests) const;
};
