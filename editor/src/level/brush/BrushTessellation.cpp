#include "BrushTessellation.h"

#include <cstddef>
#include <vector>

void BrushTessellate(const BrushMesh& mesh, const Transform3f& transform, const BrushFaceEmit& emit)
{
    std::vector<BrushTriVertex> triangles; // reused across faces

    for (const BrushFace& face : mesh.Faces)
    {
        const std::size_t n = face.Loop.size();
        if (n < 3)
            continue;

        const Vec3d worldNormal = transform.Rotation.RotateVector(face.Normal);

        const auto vertexAt = [&](std::uint32_t index) {
            const Vec3d local = mesh.Vertices[index].Position;
            return BrushTriVertex{
                .Position = transform.TransformPoint(local),
                .Normal = worldNormal,
                .Uv = ProjectUv(face.Material.Uv, local),
            };
        };

        triangles.clear();
        triangles.reserve((n - 2) * 3);
        const BrushTriVertex base = vertexAt(face.Loop[0]);
        for (std::size_t i = 1; i + 1 < n; ++i)
        {
            triangles.push_back(base);
            triangles.push_back(vertexAt(face.Loop[i]));
            triangles.push_back(vertexAt(face.Loop[i + 1]));
        }

        emit(face.Material, triangles);
    }
}
