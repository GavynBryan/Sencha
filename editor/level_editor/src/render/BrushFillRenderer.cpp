#include "BrushFillRenderer.h"

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
            [&](std::uint32_t, const FaceMaterial&, std::span<const BrushTriVertex> triangles)
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

void BrushFillRenderer::DrawZoneOverlay(const FrameContext& frame,
                                        const EditorViewport& viewport,
                                        const CameraRenderData& camera,
                                        const EditorScene& scene, const Vec4& color)
{
    // Intentional immediate path: the context-zone wash tessellates every
    // piece each frame rather than reading the retained draw set. It is the
    // one per-frame piece walk the editor keeps, counted by PieceWalks so it
    // cannot grow quietly; draw it from the context builder's queue with a
    // tint when it shows up in a profile.
    std::vector<EditorLineVertex> vertices;
    ForEachVisibleBrush(scene, /*skipLocked*/ false,
        [&](EntityId, const BrushMesh& mesh, const Transform3f& transform)
        {
            AppendBrushFill(vertices, mesh, transform, color);
        });
    if (!vertices.empty())
        Fills.Submit(frame, viewport, camera, vertices, /*onTop*/ false);
}
