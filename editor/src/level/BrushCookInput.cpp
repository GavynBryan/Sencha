#include "BrushCookInput.h"

#include "SceneBrushWalk.h"
#include "../brush/BrushTessellation.h"
#include "../brush/FaceMaterial.h"

#include <span>

std::vector<CookBrushGeometry> CollectCookBrushes(const LevelScene& scene, const AssetRef& levelDefault)
{
    std::vector<CookBrushGeometry> brushes;

    // Renderers pass skipLocked=false; the cook follows suit — a locked brush is
    // still part of the level geometry, locking is an editing affordance only.
    ForEachVisibleBrush(scene, /*skipLocked*/ false,
        [&](EntityId, const BrushMesh& mesh, const Transform3f& transform) {
            CookBrushGeometry brush;
            brush.WorldBounds = Aabb3d::Empty();

            BrushTessellate(mesh, transform,
                [&](const FaceMaterial& material, std::span<const BrushTriVertex> triangles) {
                    CookFace face;
                    face.Material = EffectiveMaterial(material, levelDefault);
                    face.Triangles.reserve(triangles.size());
                    for (const BrushTriVertex& v : triangles)
                    {
                        face.Triangles.push_back(StaticMeshVertex{
                            .Position = v.Position,
                            .Normal = v.Normal,
                            .Uv0 = v.Uv,
                            .Tangent = Vec4{},
                        });
                        brush.WorldBounds.ExpandToInclude(v.Position);
                    }
                    brush.Faces.push_back(std::move(face));
                });

            // An all-faces-deleted brush yields no geometry; drop it so it never
            // becomes an empty cell assignment.
            if (!brush.Faces.empty())
                brushes.push_back(std::move(brush));
        });

    return brushes;
}
