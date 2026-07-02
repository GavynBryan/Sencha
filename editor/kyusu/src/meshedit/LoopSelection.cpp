#include "LoopSelection.h"

#include "MeshElements.h"
#include "brush/BrushOps.h"

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

    // Map each loop edge (sorted vertex pair) to its global edge index. Both sides
    // produce sorted (min, max) pairs, so the keys line up directly.
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> edgeIndex;
    for (const EdgeElement& edge : MeshElements::Edges(mesh, transform))
        edgeIndex[{ edge.VertexA, edge.VertexB }] = edge.Index;

    std::vector<SelectableRef> refs;
    refs.reserve(loop.size());
    for (const std::array<std::uint32_t, 2>& edge : loop)
    {
        const auto it = edgeIndex.find({ edge[0], edge[1] });
        if (it != edgeIndex.end())
            refs.push_back(SelectableRef::EdgeSelection(registry, entity, it->second));
    }
    return refs;
}
