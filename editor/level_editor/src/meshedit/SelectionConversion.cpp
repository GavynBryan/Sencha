#include "SelectionConversion.h"

#include "MeshElements.h"

#include "brush/BrushMesh.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>

namespace
{
std::pair<std::uint32_t, std::uint32_t> SortedPair(std::uint32_t a, std::uint32_t b)
{
    return { std::min(a, b), std::max(a, b) };
}

SelectableKind TargetKind(MeshElementKind to)
{
    switch (to)
    {
    case MeshElementKind::Vertex: return SelectableKind::Vertex;
    case MeshElementKind::Edge:   return SelectableKind::Edge;
    case MeshElementKind::Face:   return SelectableKind::Face;
    default:                      return SelectableKind::Entity;
    }
}

// Shared topology lookups, built lazily: most conversions need only one of them.
struct MeshTopology
{
    explicit MeshTopology(const BrushMesh& mesh) : Mesh(mesh) {}

    const std::vector<EdgeElement>& Edges()
    {
        // Edge indices follow MeshElements::Edges enumeration (topology-only, so
        // the identity transform is exact); every selection ref uses that rule.
        if (!EdgeList.has_value())
            EdgeList = MeshElements::Edges(Mesh, Transform3f::Identity());
        return *EdgeList;
    }

    const std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t>& EdgeIndex()
    {
        if (!EdgeIndexMap.has_value())
        {
            EdgeIndexMap.emplace();
            for (const EdgeElement& edge : Edges())
                (*EdgeIndexMap)[SortedPair(edge.VertexA, edge.VertexB)] = edge.Index;
        }
        return *EdgeIndexMap;
    }

    const BrushMesh& Mesh;
    std::optional<std::vector<EdgeElement>> EdgeList;
    std::optional<std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t>> EdgeIndexMap;
};

void AppendUnique(std::vector<SelectableRef>& out, SelectableRef ref)
{
    if (std::find(out.begin(), out.end(), ref) == out.end())
        out.push_back(ref);
}

void ConvertOne(MeshTopology& topo, const SelectableRef& ref, SelectableKind target,
                std::vector<SelectableRef>& out)
{
    const BrushMesh& mesh = topo.Mesh;
    const auto emit = [&](SelectableKind kind, std::uint32_t element)
    {
        AppendUnique(out, SelectableRef{
            .Registry = ref.Registry,
            .Entity = ref.Entity,
            .Kind = kind,
            .ElementId = element,
        });
    };

    if (ref.Kind == SelectableKind::Face)
    {
        if (ref.ElementId >= mesh.Faces.size())
            return;
        const std::vector<std::uint32_t>& loop = mesh.Faces[ref.ElementId].Loop;
        if (target == SelectableKind::Vertex)
        {
            for (std::uint32_t v : loop)
                emit(target, v);
        }
        else // Edge: the face's boundary
        {
            const auto& edgeIndex = topo.EdgeIndex();
            for (std::size_t i = 0; i < loop.size(); ++i)
            {
                const auto it = edgeIndex.find(SortedPair(loop[i], loop[(i + 1) % loop.size()]));
                if (it != edgeIndex.end())
                    emit(target, it->second);
            }
        }
        return;
    }

    if (ref.Kind == SelectableKind::Edge)
    {
        const std::vector<EdgeElement>& edges = topo.Edges();
        if (ref.ElementId >= edges.size())
            return;
        const EdgeElement& edge = edges[ref.ElementId];
        if (target == SelectableKind::Vertex)
        {
            emit(target, edge.VertexA);
            emit(target, edge.VertexB);
        }
        else // Face: the faces sharing this edge
        {
            for (std::uint32_t f = 0; f < mesh.Faces.size(); ++f)
            {
                const std::vector<std::uint32_t>& loop = mesh.Faces[f].Loop;
                for (std::size_t i = 0; i < loop.size(); ++i)
                    if (SortedPair(loop[i], loop[(i + 1) % loop.size()])
                        == SortedPair(edge.VertexA, edge.VertexB))
                    {
                        emit(target, f);
                        break;
                    }
            }
        }
        return;
    }

    // Vertex source: incident edges or faces.
    if (ref.ElementId >= mesh.Vertices.size())
        return;
    if (target == SelectableKind::Edge)
    {
        for (const EdgeElement& edge : topo.Edges())
            if (edge.VertexA == ref.ElementId || edge.VertexB == ref.ElementId)
                emit(target, edge.Index);
    }
    else // Face: the faces whose loop contains this vertex
    {
        for (std::uint32_t f = 0; f < mesh.Faces.size(); ++f)
        {
            const std::vector<std::uint32_t>& loop = mesh.Faces[f].Loop;
            if (std::find(loop.begin(), loop.end(), ref.ElementId) != loop.end())
                emit(target, f);
        }
    }
}
}

std::vector<SelectableRef> ConvertElementRefs(const BrushMesh& mesh,
                                              std::span<const SelectableRef> refs,
                                              MeshElementKind to)
{
    const SelectableKind target = TargetKind(to);
    MeshTopology topo(mesh);

    std::vector<SelectableRef> out;
    out.reserve(refs.size());
    for (const SelectableRef& ref : refs)
    {
        if (!ref.IsValid())
            continue;
        if (target == SelectableKind::Entity)
        {
            AppendUnique(out, SelectableRef::EntitySelection(ref.Registry, ref.Entity));
            continue;
        }
        if (!ref.IsMeshElement() || ref.Kind == target)
        {
            AppendUnique(out, ref);
            continue;
        }
        ConvertOne(topo, ref, target, out);
    }
    return out;
}

std::vector<EntityRefGroup> GroupSelectionByEntity(const SelectionSnapshot& selection,
                                                   SelectableKind kind)
{
    std::vector<EntityRefGroup> groups;
    const auto groupFor = [&](EntityId entity) -> EntityRefGroup&
    {
        for (EntityRefGroup& group : groups)
            if (group.Entity == entity)
                return group;
        groups.push_back(EntityRefGroup{ .Entity = entity, .Refs = {} });
        return groups.back();
    };

    // Seed the primary's entity so its group leads: single-target consumers
    // that take the first group keep acting on the primary's mesh.
    if (selection.Primary.IsValid() && selection.Primary.Kind == kind)
        groupFor(selection.Primary.Entity);

    for (const SelectableRef& ref : selection.Items)
        if (ref.IsValid() && ref.Kind == kind)
            groupFor(ref.Entity).Refs.push_back(ref);

    std::erase_if(groups, [](const EntityRefGroup& group) { return group.Refs.empty(); });
    return groups;
}
