#include "BrushMesh.h"

#include <algorithm>

Vec3d BrushComputeFaceNormal(const BrushMesh& mesh, const BrushFace& face)
{
    // Newell's method: robust for non-planar / concave polygons, and orientation
    // follows the loop winding (CCW → outward by right-hand rule).
    Vec3d normal{ 0.0f, 0.0f, 0.0f };
    const std::size_t n = face.Loop.size();
    if (n < 3)
        return normal;

    for (std::size_t i = 0; i < n; ++i)
    {
        const Vec3d& a = mesh.Vertices[face.Loop[i]].Position;
        const Vec3d& b = mesh.Vertices[face.Loop[(i + 1) % n]].Position;
        normal.X += (a.Y - b.Y) * (a.Z + b.Z);
        normal.Y += (a.Z - b.Z) * (a.X + b.X);
        normal.Z += (a.X - b.X) * (a.Y + b.Y);
    }

    if (normal.SqrMagnitude() <= 0.0f)
        return Vec3d{ 0.0f, 0.0f, 0.0f };
    return normal.Normalized();
}

Vec3d BrushFaceCentroid(const BrushMesh& mesh, const BrushFace& face)
{
    Vec3d sum{ 0.0f, 0.0f, 0.0f };
    if (face.Loop.empty())
        return sum;
    for (std::uint32_t index : face.Loop)
        sum += mesh.Vertices[index].Position;
    return sum * (1.0f / static_cast<float>(face.Loop.size()));
}

Vec3d BrushMeshCentroid(const BrushMesh& mesh)
{
    Vec3d sum{ 0.0f, 0.0f, 0.0f };
    if (mesh.Vertices.empty())
        return sum;
    for (const BrushVertex& vertex : mesh.Vertices)
        sum += vertex.Position;
    return sum * (1.0f / static_cast<float>(mesh.Vertices.size()));
}

Aabb3d BrushComputeBounds(const BrushMesh& mesh)
{
    Aabb3d bounds = Aabb3d::Empty();
    for (const BrushVertex& vertex : mesh.Vertices)
        bounds.ExpandToInclude(vertex.Position);
    return bounds;
}

bool BrushEdgeIsSoft(const BrushMesh& mesh, std::uint32_t a, std::uint32_t b)
{
    const std::array<std::uint32_t, 2> key = BrushSoftEdgeKey(a, b);
    return std::find(mesh.SoftEdges.begin(), mesh.SoftEdges.end(), key) != mesh.SoftEdges.end();
}

void BrushSetEdgeSoft(BrushMesh& mesh, std::uint32_t a, std::uint32_t b, bool soft)
{
    const std::array<std::uint32_t, 2> key = BrushSoftEdgeKey(a, b);
    const auto it = std::find(mesh.SoftEdges.begin(), mesh.SoftEdges.end(), key);
    if (soft && it == mesh.SoftEdges.end())
        mesh.SoftEdges.push_back(key);
    else if (!soft && it != mesh.SoftEdges.end())
        mesh.SoftEdges.erase(it);
}
