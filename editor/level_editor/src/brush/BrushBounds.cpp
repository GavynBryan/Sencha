#include "BrushBounds.h"

Aabb3d BrushWorldBounds(const BrushMesh& mesh, const Transform3f& transform)
{
    Aabb3d bounds = Aabb3d::Empty();
    for (const BrushVertex& vertex : mesh.Vertices)
        bounds.ExpandToInclude(transform.TransformPoint(vertex.Position));
    return bounds;
}
