#include "ElementGeometry.h"

#include "MeshElements.h"

std::vector<std::uint32_t> ElementVertexIndices(const BrushMesh& mesh,
                                                const Transform3f& transform,
                                                const SelectableRef& ref)
{
    switch (ref.Kind)
    {
    case SelectableKind::Vertex:
        if (ref.ElementId < mesh.Vertices.size())
            return { ref.ElementId };
        return {};
    case SelectableKind::Edge:
        if (const std::optional<EdgeElement> edge = MeshElements::TryGetEdge(mesh, transform, ref.ElementId))
            return { edge->VertexA, edge->VertexB };
        return {};
    case SelectableKind::Face:
        if (ref.ElementId < mesh.Faces.size())
            return mesh.Faces[ref.ElementId].Loop;
        return {};
    case SelectableKind::Entity:
    default:
        return {};
    }
}

std::optional<Vec3d> ElementCenter(const BrushMesh& mesh,
                                   const Transform3f& transform,
                                   const SelectableRef& ref)
{
    switch (ref.Kind)
    {
    case SelectableKind::Vertex:
        if (const auto vertex = MeshElements::TryGetVertex(mesh, transform, ref.ElementId))
            return vertex->Position;
        return std::nullopt;
    case SelectableKind::Edge:
        if (const auto edge = MeshElements::TryGetEdge(mesh, transform, ref.ElementId))
            return edge->Mid;
        return std::nullopt;
    case SelectableKind::Face:
        if (const auto face = MeshElements::TryGetFace(mesh, transform, ref.ElementId))
            return face->Center;
        return std::nullopt;
    case SelectableKind::Entity:
    default:
        return std::nullopt;
    }
}
