#include "LoopSelection.h"

#include "MeshElements.h"
#include "brush/BrushOps.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>

std::vector<SelectableRef> GatherLoopSelection(const BrushMesh& mesh,
                                               const Transform3f& transform,
                                               const SelectableRef& seedEdge,
                                               MeshElementKind mode)
{
    if (!seedEdge.IsEdge())
        return {};

    const std::optional<EdgeElement> seed = MeshElements::TryGetEdge(mesh, transform, seedEdge.ElementId);
    if (!seed.has_value())
        return {};

    const RegistryId registry = seedEdge.Registry;
    const EntityId entity = seedEdge.Entity;

    if (mode == MeshElementKind::Face)
    {
        const BrushOps::BrushEdgeRing ring = BrushOps::TraceEdgeRing(mesh, seed->VertexA, seed->VertexB);
        std::vector<SelectableRef> refs;
        refs.reserve(ring.StripFaces.size());
        for (std::uint32_t face : ring.StripFaces)
            refs.push_back(SelectableRef::FaceSelection(registry, entity, face));
        return refs;
    }

    const std::vector<std::array<std::uint32_t, 2>> loop =
        BrushOps::TraceEdgeLoop(mesh, seed->VertexA, seed->VertexB);

    // A sharp rectangular inset has no straight-through continuation at its
    // corners. In that case select the smallest adjacent quad's boundary. This
    // keeps Alt-click on a doorway or window outline on that outline instead of
    // crossing its perpendicular face band.
    std::vector<std::array<std::uint32_t, 2>> edgesToSelect = loop;
    if (edgesToSelect.size() <= 1)
    {
        std::optional<std::uint32_t> boundaryFace;
        double bestArea = 0.0;
        for (std::uint32_t faceIndex = 0; faceIndex < mesh.Faces.size(); ++faceIndex)
        {
            const std::vector<std::uint32_t>& face = mesh.Faces[faceIndex].Loop;
            if (face.size() != 4)
                continue;
            bool containsSeed = false;
            for (std::size_t i = 0; i < face.size(); ++i)
            {
                const std::uint32_t a = face[i];
                const std::uint32_t b = face[(i + 1) % face.size()];
                containsSeed |= (a == seed->VertexA && b == seed->VertexB)
                    || (a == seed->VertexB && b == seed->VertexA);
            }
            if (!containsSeed)
                continue;
            const Vec3d p0 = mesh.Vertices[face[0]].Position;
            const double area = (mesh.Vertices[face[1]].Position - p0)
                    .Cross(mesh.Vertices[face[2]].Position - p0).Magnitude() * 0.5
                + (mesh.Vertices[face[2]].Position - p0)
                    .Cross(mesh.Vertices[face[3]].Position - p0).Magnitude() * 0.5;
            if (!boundaryFace || area < bestArea)
            {
                boundaryFace = faceIndex;
                bestArea = area;
            }
        }
        if (boundaryFace)
        {
            const std::vector<std::uint32_t>& face = mesh.Faces[*boundaryFace].Loop;
            edgesToSelect.clear();
            for (std::size_t i = 0; i < face.size(); ++i)
            {
                const std::uint32_t a = face[i];
                const std::uint32_t b = face[(i + 1) % face.size()];
                edgesToSelect.push_back({ std::min(a, b), std::max(a, b) });
            }
        }
    }

    // Map each loop edge (sorted vertex pair) to its global edge index. Both sides
    // produce sorted (min, max) pairs, so the keys line up directly.
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> edgeIndex;
    for (const EdgeElement& edge : MeshElements::Edges(mesh, transform))
        edgeIndex[{ edge.VertexA, edge.VertexB }] = edge.Index;

    std::vector<SelectableRef> refs;
    refs.reserve(edgesToSelect.size());
    for (const std::array<std::uint32_t, 2>& edge : edgesToSelect)
    {
        const auto it = edgeIndex.find({ edge[0], edge[1] });
        if (it != edgeIndex.end())
            refs.push_back(SelectableRef::EdgeSelection(registry, entity, it->second));
    }
    return refs;
}
