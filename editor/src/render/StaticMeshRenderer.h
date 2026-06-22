#pragma once

#include "EditorSolidPipeline.h"

#include "../viewport/EditorViewport.h"

#include <assets/static_mesh/MeshLoader.h>
#include <graphics/vulkan/Renderer.h>
#include <render/static_mesh/MeshGeometry.h>

#include <string>
#include <unordered_map>

class LevelScene;
class AssetSystem;
class AssetRegistry;
class LoggingProvider;

// Draws the document's StaticMeshComponents as solid geometry in each viewport,
// so a placed mesh is visible where it sits (WYSIWYG), through the same solid
// pipeline the brushes use. The mesh path is resolved from the component's
// handle via the shared asset system; the .smesh CPU geometry is read once and
// cached. Editor preview only: at play time the runtime renders these entities
// from the cooked scene. Inert without an asset system (brush-only documents).
class StaticMeshRenderer
{
public:
    StaticMeshRenderer(LevelScene& scene,
                       EditorSolidPipeline& solid,
                       LoggingProvider& logging,
                       AssetSystem* assets,
                       const AssetRegistry* catalog);

    void DrawViewport(const FrameContext& frame, const EditorViewport& viewport);

private:
    // CPU geometry for an asset:// mesh path, read from its file once. An empty
    // geometry (load failure or unknown path) is cached too, so we don't retry it
    // every frame; the accessor returns null for those. Bounded by the distinct
    // mesh paths referenced this session, never evicted; revisit if a long editor
    // session on a large project pushes memory.
    const MeshGeometry* GeometryFor(const std::string& assetPath);

    LevelScene& Scene;
    EditorSolidPipeline& Solid;
    MeshLoader Loader;
    AssetSystem* Assets = nullptr;
    const AssetRegistry* Catalog = nullptr;
    std::unordered_map<std::string, MeshGeometry> Cache;
};
