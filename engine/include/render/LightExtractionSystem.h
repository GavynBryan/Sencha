#pragma once

#include <render/Camera.h>
#include <render/RenderLight.h>

#include <span>

struct Registry;

// Gathers visible point and spot lights across the active registry set, ranks
// them deterministically, and packs the fixed forward-light budget.
class LightExtractionSystem
{
public:
    void Extract(std::span<Registry*> registries,
                 const CameraRenderData& camera,
                 RenderLightSet& lights) const;
};
