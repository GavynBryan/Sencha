#include "SceneWireframeRenderer.h"

#include "BrushDrawSet.h"
#include "EditorTheme.h"
#include "SceneRenderQueueBuilder.h"

#include "document/EditorScene.h"
#include "overlay/EditorOverlayState.h"
#include "selection/SelectionService.h"

#include <algorithm>

SceneWireframeRenderer::SceneWireframeRenderer(SelectionService& selection,
                                               const EditorOverlayState& overlay,
                                               const SceneRenderQueueBuilder& queues,
                                               EditorInstancedLinePipeline& lines)
    : Selection(selection)
    , Overlay(overlay)
    , Queues(queues)
    , Lines(lines)
{
}

void SceneWireframeRenderer::DrawViewport(const FrameContext& frame, const EditorViewport& viewport,
                                          const CameraRenderData& camera,
                                          const EditorScene& scene)
{
    DrawWireframe(frame, viewport, camera, scene, Queues.BrushDraws(), Vec4(1.0f, 0.0f, 0.0f, 1.0f));
}

void SceneWireframeRenderer::DrawWireframe(const FrameContext& frame, const EditorViewport& viewport,
                                           const CameraRenderData& camera, const EditorScene& scene,
                                           const BrushDrawSet& draws, const Vec4& color)
{
    // Brushes whose full wireframe the selection/hover already draws (bright anti-aliased
    // wide lines) get skipped here: the plain red one under them just doubles every edge
    // (the line and wide-line pipelines do not land on the same pixels), reading as a
    // jagged double line. Skip selected bodies, the edge-cut preview body, and a brush
    // hovered as a whole object (element hovers only light one edge/face, so keep those).
    std::vector<EntityId> skip;
    for (const SelectableRef& ref : Selection.GetSelection())
        if (ref.IsValid() && ref.Registry == scene.GetRegistry().Id && ref.Entity.IsValid())
            skip.push_back(ref.Entity);
    if (Overlay.HoverBody.IsValid())
        skip.push_back(Overlay.HoverBody);
    if (const SelectableRef hover = Overlay.Hover.Element;
        hover.IsValid() && hover.Registry == scene.GetRegistry().Id && hover.Entity.IsValid()
        && !hover.IsFace() && !hover.IsEdge() && !hover.IsVertex())
        skip.push_back(hover.Entity);

    for (const BrushDrawEntity& entity : draws.Entities())
    {
        if (std::find(skip.begin(), skip.end(), entity.Key.Entity) != skip.end())
            continue;
        for (const BrushMeshRun& run : entity.Runs)
        {
            const BrushDrawMesh& mesh = entity.Meshes[run.MeshIndex];
            if (mesh.Edges.empty() || run.PlacementCount == 0)
                continue;
            EdgeScratch.clear();
            EdgeScratch.reserve(mesh.Edges.size());
            for (const BrushEdgeVertex& edge : mesh.Edges)
                EdgeScratch.push_back(EditorLineVertex{
                    .Position = edge.Position,
                    .Color = edge.Soft ? EditorTheme::SoftEdgeWireframe : color });
            // InstanceRows holds Mat4 by rows, the EditorLineInstance layout.
            const std::span<const EditorLineInstance> instances(
                reinterpret_cast<const EditorLineInstance*>(entity.InstanceRows.data() + run.FirstPlacement),
                run.PlacementCount);
            Lines.Submit(frame, viewport, camera, EdgeScratch, instances);
        }
    }
}
