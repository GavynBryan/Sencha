#pragma once

#include <app/GameContexts.h>
#include <math/geometry/3d/Transform3d.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPropagationOrderService.h>
#include <world/transform/TransformPropagationSystem.h>
#include <world/transform/TransformStore.h>

//=============================================================================
// TransformPropagationPass
//
// Runs transform hierarchy propagation during simulation and render extraction.
// Keeps world-space transforms current across each active registry.
//=============================================================================
class TransformPropagationPass
{
public:
    void PostFixed(PostFixedContext& ctx);
    void ExtractRender(RenderExtractContext& ctx);

private:
    static void Propagate(std::span<Registry*> registries);
};
