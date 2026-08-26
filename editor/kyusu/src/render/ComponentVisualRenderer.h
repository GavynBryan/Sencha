#pragma once

#include "EditorLinePipeline.h"

#include "document/EditorScene.h"
#include "viewport/EditorViewport.h"

#include <graphics/vulkan/Renderer.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Draws per-component editor visuals as wireframe at each entity's transform via
// the shared editor line pipeline. Generic: it iterates the component-serializer
// registry and renders any EditorVisual a component advertises (today: Camera ->
// camera.glb), naming no component type. Mesh assets are imported once (glTF) and
// cached as edge lists. (consumes IComponentSerializer::GetEditorVisual.)
class ComponentVisualRenderer
{
public:
    explicit ComponentVisualRenderer(EditorLinePipeline& lines);

    // tint modulates the visual color (context-zone dimming); pass white for
    // the focus zone.
    void DrawViewport(const FrameContext& frame, const EditorViewport& viewport,
                      const CameraRenderData& camera,
                      const EditorScene& scene, const Vec4& tint);

private:
    // A mesh asset reduced to unique undirected edges in local space.
    struct MeshEdges
    {
        std::vector<Vec3d> Positions;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> Edges;
    };

    const MeshEdges& EdgesFor(std::string_view assetPath);

    EditorLinePipeline& Lines;
    std::unordered_map<std::string, MeshEdges> Cache;
};
