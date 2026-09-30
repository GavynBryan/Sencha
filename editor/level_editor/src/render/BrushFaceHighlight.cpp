#include "BrushFaceHighlight.h"

#include "brush/BrushTessellation.h"

#include <span>

BrushFaceHighlight BuildBrushFaceHighlight(const BrushEvaluated& evaluated,
                                           std::uint32_t sourceFace,
                                           std::uint64_t topologyRevision)
{
    BrushFaceHighlight out;
    out.TopologyRevision = topologyRevision;
    out.SourceFace = sourceFace;
    for (std::uint32_t m = 0; m < evaluated.Meshes.size(); ++m)
    {
        const BrushEvaluatedMesh& mesh = evaluated.Meshes[m];
        if (mesh.Mesh == nullptr)
            continue;
        // The source -> evaluated inverse of the map, for this face only: the
        // identity map answers directly, a table is scanned once here.
        std::vector<std::uint32_t> faces;
        if (mesh.ToSource.Faces == BrushElementMapKind::Identity)
        {
            if (sourceFace < mesh.Mesh->Faces.size())
                faces.push_back(sourceFace);
        }
        else if (mesh.ToSource.Faces == BrushElementMapKind::Table)
        {
            for (std::uint32_t f = 0; f < mesh.ToSource.FaceTable.size() && f < mesh.Mesh->Faces.size(); ++f)
                if (mesh.ToSource.FaceTable[f] == sourceFace)
                    faces.push_back(f);
        }
        if (faces.empty())
            continue;

        BrushFaceHighlightMesh geometry;
        geometry.MeshIndex = m;
        for (const std::uint32_t f : faces)
        {
            const std::vector<std::uint32_t>& loop = mesh.Mesh->Faces[f].Loop;
            for (std::size_t i = 0; i < loop.size(); ++i)
            {
                geometry.Outline.push_back(mesh.Mesh->Vertices[loop[i]].Position);
                geometry.Outline.push_back(mesh.Mesh->Vertices[loop[(i + 1) % loop.size()]].Position);
            }
            BrushTessellateFace(*mesh.Mesh, Transform3f::Identity(), f,
                [&](std::uint32_t, const FaceMaterial&, std::span<const BrushTriVertex> triangles)
                {
                    for (const BrushTriVertex& v : triangles)
                        geometry.Fill.push_back(v.Position);
                });
        }
        out.Meshes.push_back(std::move(geometry));
    }
    return out;
}
