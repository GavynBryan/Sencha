#include "PathSelection.h"

#include "MeshElements.h"

#include "brush/BrushMesh.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <utility>

namespace
{
using Adjacency = std::vector<std::vector<std::uint32_t>>;

std::pair<std::uint32_t, std::uint32_t> SortedPair(std::uint32_t a, std::uint32_t b)
{
    return { std::min(a, b), std::max(a, b) };
}

// Element indices follow the shared enumeration rules: vertices and faces index
// the mesh arrays directly; edges use MeshElements::Edges order (which is
// topology-only, so the identity transform is exact).
std::vector<EdgeElement> EnumerateEdges(const BrushMesh& mesh)
{
    return MeshElements::Edges(mesh, Transform3f::Identity());
}

Adjacency VertexAdjacency(const BrushMesh& mesh, const std::vector<EdgeElement>& edges)
{
    Adjacency adj(mesh.Vertices.size());
    for (const EdgeElement& edge : edges)
    {
        adj[edge.VertexA].push_back(edge.VertexB);
        adj[edge.VertexB].push_back(edge.VertexA);
    }
    return adj;
}

Adjacency EdgeAdjacency(const BrushMesh& mesh, const std::vector<EdgeElement>& edges)
{
    std::map<std::uint32_t, std::vector<std::uint32_t>> vertexEdges;
    for (const EdgeElement& edge : edges)
    {
        vertexEdges[edge.VertexA].push_back(edge.Index);
        vertexEdges[edge.VertexB].push_back(edge.Index);
    }

    Adjacency adj(edges.size());
    for (const auto& [vertex, incident] : vertexEdges)
        for (std::uint32_t a : incident)
            for (std::uint32_t b : incident)
                if (a != b)
                    adj[a].push_back(b);
    return adj;
}

Adjacency FaceAdjacency(const BrushMesh& mesh)
{
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<std::uint32_t>> edgeFaces;
    for (std::uint32_t f = 0; f < mesh.Faces.size(); ++f)
    {
        const std::vector<std::uint32_t>& loop = mesh.Faces[f].Loop;
        for (std::size_t i = 0; i < loop.size(); ++i)
            edgeFaces[SortedPair(loop[i], loop[(i + 1) % loop.size()])].push_back(f);
    }

    Adjacency adj(mesh.Faces.size());
    for (const auto& [edge, faces] : edgeFaces)
        for (std::uint32_t a : faces)
            for (std::uint32_t b : faces)
                if (a != b)
                    adj[a].push_back(b);
    return adj;
}

// BFS with parent trace. Neighbors expand in ascending index order so equal-length
// paths always resolve the same way.
std::vector<std::uint32_t> ShortestPath(Adjacency adj, std::uint32_t from, std::uint32_t to)
{
    if (from >= adj.size() || to >= adj.size())
        return {};

    for (std::vector<std::uint32_t>& neighbors : adj)
    {
        std::sort(neighbors.begin(), neighbors.end());
        neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    }

    constexpr std::uint32_t kUnvisited = 0xFFFFFFFFu;
    std::vector<std::uint32_t> parent(adj.size(), kUnvisited);
    std::deque<std::uint32_t> frontier{ from };
    parent[from] = from;
    while (!frontier.empty() && parent[to] == kUnvisited)
    {
        const std::uint32_t node = frontier.front();
        frontier.pop_front();
        for (std::uint32_t next : adj[node])
            if (parent[next] == kUnvisited)
            {
                parent[next] = node;
                frontier.push_back(next);
            }
    }
    if (parent[to] == kUnvisited)
        return {};

    std::vector<std::uint32_t> path{ to };
    while (path.back() != from)
        path.push_back(parent[path.back()]);
    std::reverse(path.begin(), path.end());
    return path;
}
}

std::vector<SelectableRef> GatherPathSelection(const BrushMesh& mesh,
                                               const SelectableRef& from,
                                               const SelectableRef& to)
{
    if (!from.IsMeshElement() || from.Kind != to.Kind
        || from.Registry != to.Registry || from.Entity != to.Entity)
        return {};

    std::vector<std::uint32_t> path;
    switch (from.Kind)
    {
    case SelectableKind::Vertex:
        path = ShortestPath(VertexAdjacency(mesh, EnumerateEdges(mesh)), from.ElementId, to.ElementId);
        break;
    case SelectableKind::Edge:
        path = ShortestPath(EdgeAdjacency(mesh, EnumerateEdges(mesh)), from.ElementId, to.ElementId);
        break;
    case SelectableKind::Face:
        path = ShortestPath(FaceAdjacency(mesh), from.ElementId, to.ElementId);
        break;
    default:
        return {};
    }

    std::vector<SelectableRef> refs;
    refs.reserve(path.size());
    for (std::uint32_t element : path)
        refs.push_back(SelectableRef{
            .Registry = from.Registry,
            .Entity = from.Entity,
            .Kind = from.Kind,
            .ElementId = element,
        });
    return refs;
}
