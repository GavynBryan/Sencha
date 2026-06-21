#include "BrushGeometry.h"

Aabb3d BrushGeometry::ComputeWorldBounds(const BrushMesh& mesh, const Transform3f& transform)
{
    Aabb3d bounds = Aabb3d::Empty();
    for (const BrushVertex& vertex : mesh.Vertices)
        bounds.ExpandToInclude(transform.TransformPoint(vertex.Position));
    return bounds;
}

std::optional<Aabb3d> BrushGeometry::ComputeWorldBounds(const LevelScene& scene, EntityId entity)
{
    const BrushMesh* mesh = scene.TryGetBrushMesh(entity);
    const Transform3f* transform = scene.TryGetTransform(entity);
    if (mesh == nullptr || transform == nullptr || mesh->Vertices.empty())
        return std::nullopt;

    return ComputeWorldBounds(*mesh, *transform);
}
