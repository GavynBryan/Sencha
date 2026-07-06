#include "MeshElements.h"

#include "brush/BrushHalfEdge.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace
{
Vec3d WorldFaceNormal(const std::vector<Vec3d>& corners)
{
    Vec3d normal{ 0.0f, 0.0f, 0.0f };
    const std::size_t n = corners.size();
    if (n < 3)
        return normal;

    for (std::size_t i = 0; i < n; ++i)
    {
        const Vec3d& a = corners[i];
        const Vec3d& b = corners[(i + 1) % n];
        normal.X += (a.Y - b.Y) * (a.Z + b.Z);
        normal.Y += (a.Z - b.Z) * (a.X + b.X);
        normal.Z += (a.X - b.X) * (a.Y + b.Y);
    }

    return normal.SqrMagnitude() > 0.0f ? normal.Normalized() : Vec3d{ 0.0f, 0.0f, 0.0f };
}

std::optional<FaceElement> BuildFace(const BrushMesh& mesh,
                                     const Transform3f& transform,
                                     std::uint32_t faceIndex)
{
    if (faceIndex >= mesh.Faces.size())
        return std::nullopt;

    const BrushFace& face = mesh.Faces[faceIndex];
    FaceElement element;
    element.Index = faceIndex;
    element.Corners.reserve(face.Loop.size());

    Vec3d center{ 0.0f, 0.0f, 0.0f };
    for (std::uint32_t vertexIndex : face.Loop)
    {
        if (vertexIndex >= mesh.Vertices.size())
            return std::nullopt;

        const Vec3d world = transform.TransformPoint(mesh.Vertices[vertexIndex].Position);
        element.Corners.push_back(world);
        center += world;
    }

    if (!element.Corners.empty())
        center = center * (1.0f / static_cast<float>(element.Corners.size()));
    element.Center = center;
    element.Normal = WorldFaceNormal(element.Corners);
    return element;
}

std::vector<std::pair<std::uint32_t, std::uint32_t>> UniqueEdgeVertexPairs(const BrushMesh& mesh)
{
    std::vector<std::pair<std::uint32_t, std::uint32_t>> pairs;
    const BrushHalfEdgeMesh halfEdge = BrushBuildHalfEdge(mesh);
    pairs.reserve(halfEdge.HalfEdges.size());

    for (const BrushHalfEdge& edge : halfEdge.HalfEdges)
    {
        if (edge.Origin >= mesh.Vertices.size() || edge.Next >= halfEdge.HalfEdges.size())
            continue;

        const std::uint32_t target = halfEdge.HalfEdges[edge.Next].Origin;
        if (target >= mesh.Vertices.size() || target == edge.Origin)
            continue;

        const std::uint32_t a = std::min(edge.Origin, target);
        const std::uint32_t b = std::max(edge.Origin, target);
        pairs.emplace_back(a, b);
    }

    std::sort(pairs.begin(), pairs.end());
    pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
    return pairs;
}
}

std::vector<FaceElement> MeshElements::Faces(const BrushMesh& mesh,
                                             const Transform3f& transform)
{
    std::vector<FaceElement> faces;
    faces.reserve(mesh.Faces.size());
    for (std::uint32_t i = 0; i < mesh.Faces.size(); ++i)
    {
        if (auto face = BuildFace(mesh, transform, i))
            faces.push_back(std::move(*face));
    }
    return faces;
}

std::vector<EdgeElement> MeshElements::Edges(const BrushMesh& mesh,
                                             const Transform3f& transform)
{
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> pairs = UniqueEdgeVertexPairs(mesh);

    std::vector<EdgeElement> edges;
    edges.reserve(pairs.size());
    for (const auto& [aIndex, bIndex] : pairs)
    {
        const Vec3d a = transform.TransformPoint(mesh.Vertices[aIndex].Position);
        const Vec3d b = transform.TransformPoint(mesh.Vertices[bIndex].Position);
        edges.push_back(EdgeElement{
            .Index = static_cast<std::uint32_t>(edges.size()),
            .VertexA = aIndex,
            .VertexB = bIndex,
            .A = a,
            .B = b,
            .Mid = (a + b) * 0.5f,
        });
    }
    return edges;
}

std::vector<VertexElement> MeshElements::Vertices(const BrushMesh& mesh,
                                                  const Transform3f& transform)
{
    std::vector<VertexElement> vertices;
    vertices.reserve(mesh.Vertices.size());
    for (std::uint32_t i = 0; i < mesh.Vertices.size(); ++i)
    {
        vertices.push_back(VertexElement{
            .Index = i,
            .Position = transform.TransformPoint(mesh.Vertices[i].Position),
        });
    }
    return vertices;
}

std::optional<FaceElement> MeshElements::TryGetFace(const BrushMesh& mesh,
                                                    const Transform3f& transform,
                                                    std::uint32_t index)
{
    return BuildFace(mesh, transform, index);
}

std::optional<EdgeElement> MeshElements::TryGetEdge(const BrushMesh& mesh,
                                                    const Transform3f& transform,
                                                    std::uint32_t index)
{
    const std::vector<EdgeElement> edges = Edges(mesh, transform);
    if (index >= edges.size())
        return std::nullopt;
    return edges[index];
}

std::optional<VertexElement> MeshElements::TryGetVertex(const BrushMesh& mesh,
                                                        const Transform3f& transform,
                                                        std::uint32_t index)
{
    if (index >= mesh.Vertices.size())
        return std::nullopt;

    return VertexElement{
        .Index = index,
        .Position = transform.TransformPoint(mesh.Vertices[index].Position),
    };
}

std::vector<SelectableRef> MeshElements::AllRefs(const BrushMesh& mesh,
                                                 const Transform3f& transform,
                                                 RegistryId registry,
                                                 EntityId entity,
                                                 MeshElementKind kind)
{
    std::vector<SelectableRef> refs;
    switch (kind)
    {
    case MeshElementKind::Object:
        refs.push_back(SelectableRef::EntitySelection(registry, entity));
        break;
    case MeshElementKind::Vertex:
        refs.reserve(mesh.Vertices.size());
        for (const VertexElement& vertex : Vertices(mesh, transform))
            refs.push_back(SelectableRef::VertexSelection(registry, entity, vertex.Index));
        break;
    case MeshElementKind::Edge:
        for (const EdgeElement& edge : Edges(mesh, transform))
            refs.push_back(SelectableRef::EdgeSelection(registry, entity, edge.Index));
        break;
    case MeshElementKind::Face:
        refs.reserve(mesh.Faces.size());
        for (const FaceElement& face : Faces(mesh, transform))
            refs.push_back(SelectableRef::FaceSelection(registry, entity, face.Index));
        break;
    }
    return refs;
}
