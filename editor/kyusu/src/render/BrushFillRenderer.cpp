#include "BrushFillRenderer.h"

#include "EditorTheme.h"

#include "document/EditorScene.h"
#include "document/SceneBrushWalk.h"
#include "brush/BrushTessellation.h"
#include "brush/FaceMaterial.h"

#include <span>
#include <vector>

namespace
{
    void AppendBrushFill(std::vector<EditorLineVertex>& vertices, const BrushMesh& mesh,
                         const Transform3f& transform, const Vec4& fill)
    {
        BrushTessellate(mesh, transform,
            [&](const FaceMaterial&, std::span<const BrushTriVertex> triangles)
            {
                for (const BrushTriVertex& v : triangles)
                    vertices.push_back(EditorLineVertex{ .Position = v.Position,
                                                         .Color = fill });
            });
    }
}

BrushFillRenderer::BrushFillRenderer(EditorFillPipeline& fills)
    : Fills(fills)
{
}

void BrushFillRenderer::DrawPortalVolumes(const FrameContext& frame,
                                          const EditorViewport& viewport,
                                          const EditorScene& scene, const Vec4& tint)
{
    std::vector<EditorLineVertex> vertices;
    ForEachVisibleBrush(scene, /*skipLocked*/ false,
        [&](EntityId id, const BrushMesh& mesh, const Transform3f& transform)
        {
            if (!scene.IsPortal(id))
                return;
            const Vec4 fill(EditorTheme::PortalFill.X * tint.X,
                            EditorTheme::PortalFill.Y * tint.Y,
                            EditorTheme::PortalFill.Z * tint.Z,
                            EditorTheme::PortalFill.W);
            AppendBrushFill(vertices, mesh, transform, fill);
        });
    if (!vertices.empty())
        Fills.Submit(frame, viewport, vertices, /*onTop*/ false);
}

void BrushFillRenderer::DrawZoneOverlay(const FrameContext& frame,
                                        const EditorViewport& viewport,
                                        const EditorScene& scene, const Vec4& color)
{
    std::vector<EditorLineVertex> vertices;
    ForEachVisibleBrush(scene, /*skipLocked*/ false,
        [&](EntityId id, const BrushMesh& mesh, const Transform3f& transform)
        {
            // Portals get their own dimmed volume fill, not the wash.
            if (scene.IsPortal(id))
                return;
            AppendBrushFill(vertices, mesh, transform, color);
        });
    if (!vertices.empty())
        Fills.Submit(frame, viewport, vertices, /*onTop*/ false);
}
