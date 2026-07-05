#include "PortalVolumeRenderer.h"

#include "EditorTheme.h"

#include "document/EditorScene.h"
#include "document/SceneBrushWalk.h"
#include "brush/BrushTessellation.h"
#include "brush/FaceMaterial.h"

#include <span>
#include <vector>

PortalVolumeRenderer::PortalVolumeRenderer(EditorFillPipeline& fills)
    : Fills(fills)
{
}

void PortalVolumeRenderer::DrawViewport(const FrameContext& frame,
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
            BrushTessellate(mesh, transform,
                [&](const FaceMaterial&, std::span<const BrushTriVertex> triangles)
                {
                    for (const BrushTriVertex& v : triangles)
                        vertices.push_back(EditorLineVertex{ .Position = v.Position,
                                                             .Color = fill });
                });
        });
    if (!vertices.empty())
        Fills.Submit(frame, viewport, vertices, /*onTop*/ false);
}
