#include "BrushHalfEdge.h"

#include <cstdint>
#include <unordered_map>

namespace
{
    std::uint64_t DirectedEdgeKey(std::uint32_t origin, std::uint32_t dest)
    {
        return (static_cast<std::uint64_t>(origin) << 32) | static_cast<std::uint64_t>(dest);
    }
}

BrushHalfEdgeMesh BrushBuildHalfEdge(const BrushMesh& mesh)
{
    BrushHalfEdgeMesh out;
    out.Vertices = mesh.Vertices;
    out.FaceToHalfEdge.resize(mesh.Faces.size(), BrushInvalidIndex);

    // Maps a directed (origin→dest) edge to its half-edge index, for twin linking.
    std::unordered_map<std::uint64_t, std::uint32_t> directed;

    for (std::uint32_t faceIndex = 0; faceIndex < mesh.Faces.size(); ++faceIndex)
    {
        const BrushFace& face = mesh.Faces[faceIndex];
        const std::size_t n = face.Loop.size();
        if (n < 3)
            continue;

        const std::uint32_t base = static_cast<std::uint32_t>(out.HalfEdges.size());
        out.FaceToHalfEdge[faceIndex] = base;

        for (std::size_t i = 0; i < n; ++i)
        {
            BrushHalfEdge he;
            he.Origin = face.Loop[i];
            he.Next = base + static_cast<std::uint32_t>((i + 1) % n);
            he.Face = faceIndex;
            out.HalfEdges.push_back(he);

            const std::uint32_t dest = face.Loop[(i + 1) % n];
            directed.emplace(DirectedEdgeKey(face.Loop[i], dest),
                             base + static_cast<std::uint32_t>(i));
        }
    }

    // Twins: half-edge (a→b) pairs with (b→a) if present.
    for (std::uint32_t i = 0; i < out.HalfEdges.size(); ++i)
    {
        const BrushHalfEdge& he = out.HalfEdges[i];
        const std::uint32_t dest = out.HalfEdges[he.Next].Origin;
        auto twin = directed.find(DirectedEdgeKey(dest, he.Origin));
        if (twin != directed.end())
            out.HalfEdges[i].Twin = twin->second;
    }

    return out;
}

BrushMesh BrushToFaceVertex(const BrushHalfEdgeMesh& halfEdge)
{
    BrushMesh mesh;
    mesh.Vertices = halfEdge.Vertices;
    mesh.Faces.reserve(halfEdge.FaceToHalfEdge.size());

    for (std::uint32_t start : halfEdge.FaceToHalfEdge)
    {
        if (start == BrushInvalidIndex)
            continue;

        BrushFace face;
        std::uint32_t edge = start;
        do
        {
            face.Loop.push_back(halfEdge.HalfEdges[edge].Origin);
            edge = halfEdge.HalfEdges[edge].Next;
        } while (edge != start && edge != BrushInvalidIndex);

        face.Normal = BrushComputeFaceNormal(mesh, face);
        mesh.Faces.push_back(std::move(face));
    }

    return mesh;
}
