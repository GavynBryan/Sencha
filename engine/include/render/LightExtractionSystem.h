#pragma once

#include <ecs/Query.h>
#include <ecs/World.h>
#include <render/PointLightComponent.h>
#include <render/RenderLight.h>
#include <world/transform/TransformComponents.h>

#include <optional>

//=============================================================================
// LightExtractionSystem
//
// Walks all enabled PointLightComponents and packs them into a RenderLightSet
// for the forward pass. The lighting counterpart of RenderExtractionSystem:
// world position comes from WorldTransform (the same source the mesh extractor
// reads), so a light and the meshes it lights share one transform pipeline.
// Camera-independent; the same set serves every view.
//
// The query is cached per instance to avoid rebuild-from-scratch every frame;
// a World* sentinel detects world changes (relevant in multi-registry loops).
//=============================================================================
class LightExtractionSystem
{
public:
    void Extract(const World& world, RenderLightSet& lights);

private:
    const World* LastWorld = nullptr;
    std::optional<Query<Read<WorldTransform>, Read<PointLightComponent>>> CachedQuery;
};
