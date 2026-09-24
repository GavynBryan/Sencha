#include <assets/cook/StaticCollisionGeometry.h>

#include <assets/cook/BrushGeometryCook.h>

void AppendCellCollisionTriangles(const BrushCell& cell,
                                  CollisionTriangleSpace space,
                                  std::vector<Vec3d>& positions,
                                  std::vector<std::uint32_t>& indices)
{
    const bool world = space == CollisionTriangleSpace::World;
    for (const CookFace& face : cell.Faces)
        for (const StaticMeshVertex& vertex : face.Triangles)
        {
            indices.push_back(static_cast<std::uint32_t>(positions.size()));
            positions.push_back(world ? vertex.Position + cell.Origin
                                      : vertex.Position);
        }
}

StaticCollisionGeometry CollectStaticCollisionGeometry(std::span<const BrushCell> cells)
{
    StaticCollisionGeometry geometry;
    for (const BrushCell& cell : cells)
        AppendCellCollisionTriangles(cell, CollisionTriangleSpace::World, geometry.Positions,
                                     geometry.Indices);
    for (const Vec3d& position : geometry.Positions)
        geometry.Bounds.ExpandToInclude(position);
    return geometry;
}
