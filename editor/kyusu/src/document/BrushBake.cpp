#include "BrushBake.h"

#include "brush/BrushMesh.h"
#include "brush/BrushTessellation.h"
#include "brush/FaceMaterial.h"

#include <assets/cook/BrushGeometryCook.h>

#include <span>
#include <utility>

bool BakeBrushToGeometry(const BrushMesh& mesh,
                         const AssetRef& levelDefault,
                         MeshGeometry& out,
                         std::vector<AssetRef>& outMaterialOrder,
                         std::string* error)
{
    std::vector<CookFace> faces;
    BrushTessellate(mesh, Transform3f::Identity(),
        [&](const FaceMaterial& material, std::span<const BrushTriVertex> triangles) {
            CookFace face;
            face.Material = EffectiveMaterial(material, levelDefault);
            face.Triangles.reserve(triangles.size());
            for (const BrushTriVertex& v : triangles)
                face.Triangles.push_back(StaticMeshVertex{
                    .Position = v.Position,
                    .Normal = v.Normal,
                    .Uv0 = v.Uv,
                    .Tangent = Vec4{},
                });
            faces.push_back(std::move(face));
        });

    if (faces.empty())
    {
        if (error != nullptr)
            *error = "brush has no faces to bake";
        return false;
    }

    outMaterialOrder = CollectMaterialOrder(faces);
    return BakeBrushFacesToStaticMesh(faces, outMaterialOrder, out, error);
}
