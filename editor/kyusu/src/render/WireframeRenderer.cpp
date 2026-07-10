#include "WireframeRenderer.h"

#include "EditorTheme.h"
#include "document/SceneBrushWalk.h"

#include <algorithm>
#include <cstddef>
#include <vector>

WireframeRenderer::WireframeRenderer(SelectionService& selection,
                                     const EditorOverlayState& overlay, EditorLinePipeline& lines)
    : Selection(selection)
    , Overlay(overlay)
    , Lines(lines)
{
}

void WireframeRenderer::DrawViewport(const FrameContext& frame, const EditorViewport& viewport,
                                     const EditorScene& scene)
{
    DrawWireframe(frame, viewport, scene, Vec4(1.0f, 0.0f, 0.0f, 1.0f));
}

void WireframeRenderer::DrawWireframe(const FrameContext& frame, const EditorViewport& viewport,
                                      const EditorScene& scene, const Vec4& color)
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

    std::vector<EditorLineVertex> vertices;
    vertices.reserve(scene.GetEntityCount() * 24);
    ForEachVisibleBrush(scene, /*skipLocked*/ false,
        [&](EntityId id, const BrushMesh& mesh, const Transform3f& transform)
        {
            if (std::find(skip.begin(), skip.end(), id) != skip.end())
                return;
            AppendBrushMesh(vertices, mesh, transform, color);
        });

    Lines.Submit(frame, viewport, vertices);
}

void WireframeRenderer::AppendBrushMesh(std::vector<EditorLineVertex>& vertices,
                                        const BrushMesh& mesh,
                                        const Transform3f& transform,
                                        const Vec4& color) const
{
    for (const BrushFace& face : mesh.Faces)
    {
        const std::size_t n = face.Loop.size();
        for (std::size_t i = 0; i < n; ++i)
        {
            const std::uint32_t ia = face.Loop[i];
            const std::uint32_t ib = face.Loop[(i + 1) % n];
            const Vec3d a = transform.TransformPoint(mesh.Vertices[ia].Position);
            const Vec3d b = transform.TransformPoint(mesh.Vertices[ib].Position);
            const Vec4& stroke = !mesh.SoftEdges.empty() && BrushEdgeIsSoft(mesh, ia, ib)
                ? EditorTheme::SoftEdgeWireframe
                : color;
            vertices.push_back(EditorLineVertex{ .Position = a, .Color = stroke });
            vertices.push_back(EditorLineVertex{ .Position = b, .Color = stroke });
        }
    }
}
