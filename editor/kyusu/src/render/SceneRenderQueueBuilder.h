#pragma once

#include <render/MaterialCache.h>   // MaterialHandle
#include <render/RenderLight.h>
#include <render/RenderQueue.h>
#include <render/static_mesh/StaticMeshHandle.h>

#include <cstdint>
#include <vector>

class EditorDocument;
class AssetSystem;
class StaticMeshCache;
class MaterialSetCache;
class LoggingProvider;
class Logger;

//=============================================================================
// SceneRenderQueueBuilder
//
// Produces the editor's per-frame draw queues from the document so the Solid
// viewport renders the exact GpuStaticMesh + bindless Material the runtime
// ships (WYSIWYG). Brushes are tessellated and baked through the SAME brush
// cook kernel the offline cook and PIE use (CollectCookBrushes +
// BakeBrushFacesToStaticMesh), then uploaded to the shared StaticMeshCache;
// placed meshes are already GPU-resident (loaded through the AssetSystem), so
// their handles are emitted directly.
//
// Two queues because the per-viewport draw policy differs: brushes follow the
// viewport's shading mode (only Solid viewports draw them through here),
// placed meshes draw in every viewport. Both are camera-independent and built
// once per frame; the per-viewport camera is applied at draw time.
//
// CPU/asset only (no Vulkan) so it can be unit-tested headlessly.
//=============================================================================
class SceneRenderQueueBuilder
{
public:
    SceneRenderQueueBuilder(const EditorDocument& document,
                            AssetSystem& assets,
                            StaticMeshCache& meshes,
                            MaterialSetCache& materialSets,
                            LoggingProvider& logging);
    ~SceneRenderQueueBuilder();

    SceneRenderQueueBuilder(const SceneRenderQueueBuilder&) = delete;
    SceneRenderQueueBuilder& operator=(const SceneRenderQueueBuilder&) = delete;

    // Rebuild both queues from the current document. Brush geometry is re-baked
    // and re-uploaded only when the scene's brushes changed since the last call
    // (whole-scene content hash, so an idle frame uploads nothing); placed-mesh
    // items are re-emitted each call (their GPU meshes are owned by the asset
    // system, not here).
    void Build();

    [[nodiscard]] const RenderQueue& BrushQueue() const { return Brushes; }
    [[nodiscard]] const RenderQueue& MeshQueue() const { return PlacedMeshes; }
    [[nodiscard]] const RenderLightSet& Lights() const { return SceneLights; }
    [[nodiscard]] RenderLightSet& Lights() { return SceneLights; }

private:
    // One cooked brush's GPU mesh, owned here (Create/Destroy), plus the material
    // handle per material slot (index = StaticMeshSection::MaterialSlot).
    struct CachedBrushMesh
    {
        StaticMeshHandle Mesh;
        std::vector<MaterialHandle> SlotMaterials;
    };

    void RebuildBrushMeshes();
    void EmitBrushQueue();
    void BuildMeshQueue();
    void BuildLights();
    void ReleaseBrushMeshes();

    const EditorDocument& Document;
    AssetSystem& Assets;
    StaticMeshCache& Meshes;
    MaterialSetCache& MaterialSets;
    Logger& Log;

    std::vector<CachedBrushMesh> BrushMeshes;     // GPU brush meshes, one per cooked brush
    std::vector<MaterialHandle> BrushMaterials;   // material refs this build holds (released on rebuild)
    uint64_t BrushHash = 0;                       // content hash of the last bake
    bool HasBaked = false;

    RenderQueue Brushes;
    RenderQueue PlacedMeshes;
    RenderLightSet SceneLights;
};
