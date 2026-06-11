#pragma once

#include <render/Camera.h>
#include <render/MaterialCache.h>
#include <render/StaticMeshComponent.h>
#include <render/static_mesh/StaticMeshCache.h>
#include <render/RenderQueue.h>
#include <ecs/World.h>

//=============================================================================
// RenderExtractionSystem
//
// Stateless system that walks all visible StaticMeshComponents and emits
// one RenderQueueItem per enabled section into the RenderQueue. World-space
// bounds are computed here for use by the subsequent culling pass.
//=============================================================================
class RenderExtractionSystem
{
public:
    static void Extract(const World& world,
                        const StaticMeshCache& meshes,
                        const MaterialCache& materials,
                        const CameraRenderData& camera,
                        RenderQueue& queue);
};
