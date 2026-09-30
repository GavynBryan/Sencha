#include "ElementSnap.h"

#include "GridSettings.h"
#include "Picking.h"
#include "meshedit/ElementGeometry.h"
#include "meshedit/ManipulationSink.h"
#include "tools/ToolContext.h"

std::optional<Vec3d> ElementSnapPoint(const ToolContext& ctx,
                                      const EditorViewport& viewport,
                                      ImVec2 point)
{
    if (!ctx.Grid.SnapEnabled || ctx.Grid.Target == SnapTarget::Grid)
        return std::nullopt;

    if (ctx.Grid.Target == SnapTarget::Face)
    {
        const std::optional<SurfaceHit> hit = ctx.Picking.PickSurface(viewport, point, ctx.Scene);
        if (!hit.has_value())
            return std::nullopt;
        return hit->Point;
    }

    BrushPickRequest request;
    request.Mode = ctx.Grid.Target == SnapTarget::Vertex ? BrushPickMode::VertexOnly
                                                         : BrushPickMode::EdgeOnly;
    const SelectableRef ref = ctx.Picking.Pick(viewport, point, ctx.Scene, request);
    if (!ref.IsValid())
        return std::nullopt;

    const std::optional<MeshEditTargetMesh> resolved = ctx.Sink.ResolveMesh(ref.Entity);
    if (!resolved.has_value() || resolved->Mesh == nullptr)
        return std::nullopt;
    return ElementCenter(*resolved->Mesh, resolved->Transform, ref);
}
