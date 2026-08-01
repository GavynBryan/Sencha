#include "PendingBridgeEdit.h"

#include "commands/CommandStack.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/commands/DeferredCreateBrushCommand.h"
#include "meshedit/MeshElements.h"
#include "selection/SelectionService.h"

#include <algorithm>
#include <map>
#include <optional>
#include <utility>

namespace
{

struct SelectedEdgePath
{
    EntityId Entity;
    std::vector<std::uint32_t> Vertices;
    bool Closed = false;
    bool SourceWindsForward = false;
    FaceMaterial Material;
};

// The selected edges of one brush, grouped in first-seen order. Non-edge refs
// are ignored: the gesture is defined by the edges alone.
std::vector<std::pair<EntityId, std::vector<SelectableRef>>> GroupEdgesByBrush(
    std::span<const SelectableRef> selection)
{
    std::vector<std::pair<EntityId, std::vector<SelectableRef>>> groups;
    for (const SelectableRef& ref : selection)
    {
        if (!ref.IsEdge())
            continue;
        auto group = std::find_if(groups.begin(), groups.end(), [&](const auto& candidate)
        {
            return candidate.first == ref.Entity;
        });
        if (group == groups.end())
        {
            groups.emplace_back(ref.Entity, std::vector<SelectableRef>{});
            group = std::prev(groups.end());
        }
        group->second.push_back(ref);
    }
    return groups;
}

std::optional<SelectedEdgePath> MakeSelectedEdgePath(const EditorScene& scene, EntityId entity,
                                                      std::span<const SelectableRef> refs)
{
    const BrushMesh* mesh = scene.TryGetBrushMesh(entity);
    const Transform3f* transform = scene.TryGetTransform(entity);
    if (mesh == nullptr || transform == nullptr)
        return std::nullopt;

    std::map<std::uint32_t, std::vector<std::uint32_t>> adjacency;
    for (const SelectableRef& ref : refs)
    {
        const std::optional<EdgeElement> edge = MeshElements::TryGetEdge(*mesh, *transform, ref.ElementId);
        if (!edge.has_value())
            continue;
        adjacency[edge->VertexA].push_back(edge->VertexB);
        adjacency[edge->VertexB].push_back(edge->VertexA);
    }
    if (adjacency.size() < 2)
        return std::nullopt;
    for (auto& [vertex, neighbours] : adjacency)
    {
        std::sort(neighbours.begin(), neighbours.end());
        if (neighbours.size() > 2 || std::adjacent_find(neighbours.begin(), neighbours.end()) != neighbours.end())
            return std::nullopt;
    }

    std::vector<std::uint32_t> endpoints;
    for (const auto& [vertex, neighbours] : adjacency)
        if (neighbours.size() == 1)
            endpoints.push_back(vertex);
    const bool closed = endpoints.empty();
    if ((!closed && endpoints.size() != 2) || (closed && adjacency.size() < 3))
        return std::nullopt;

    std::vector<std::uint32_t> path;
    std::optional<std::uint32_t> previous;
    std::uint32_t current = closed ? adjacency.begin()->first : endpoints.front();
    while (true)
    {
        path.push_back(current);
        std::optional<std::uint32_t> next;
        for (std::uint32_t neighbour : adjacency.at(current))
            if (!previous.has_value() || neighbour != *previous)
            {
                next = neighbour;
                break;
            }
        if (!next || (closed && *next == path.front()))
            break;
        previous = current;
        current = *next;
        if (path.size() > adjacency.size())
            return std::nullopt;
    }
    if (path.size() != adjacency.size())
        return std::nullopt;

    FaceMaterial material;
    for (const BrushFace& face : mesh->Faces)
        for (std::size_t i = 0; i < face.Loop.size(); ++i)
            if ((face.Loop[i] == path[0] && face.Loop[(i + 1) % face.Loop.size()] == path[1])
                || (face.Loop[i] == path[1] && face.Loop[(i + 1) % face.Loop.size()] == path[0]))
            {
                const bool windsForward = face.Loop[i] == path[0]
                    && face.Loop[(i + 1) % face.Loop.size()] == path[1];
                return SelectedEdgePath{
                    entity,
                    std::move(path),
                    closed,
                    windsForward,
                    face.Material,
                };
            }
    // A path whose first edge borders no face is degenerate input; refuse it
    // rather than bridging with a made-up winding and material.
    return std::nullopt;
}

// The path resolved into world space for BuildBridgeBetweenPaths: transformed
// positions plus departure tangents from the source mesh's bordering faces.
BrushOps::BridgePathSpec MakeBridgePathSpec(const EditorScene& scene, const SelectedEdgePath& path)
{
    const BrushMesh& mesh = *scene.TryGetBrushMesh(path.Entity);
    const Transform3f& transform = *scene.TryGetTransform(path.Entity);

    BrushOps::BridgePathSpec spec;
    spec.Closed = path.Closed;
    spec.WindsForward = path.SourceWindsForward;
    spec.Positions.reserve(path.Vertices.size());
    for (std::uint32_t vertex : path.Vertices)
        spec.Positions.push_back(transform.TransformPoint(mesh.Vertices[vertex].Position));

    // Directions under the entity transform; normalize-after-transform is the
    // accepted approximation under non-uniform scale.
    spec.Tangents.reserve(path.Vertices.size());
    for (Vec3d tangent : BrushOps::PathBoundaryTangents(mesh, path.Vertices, path.Closed))
    {
        const Vec3d world = transform.TransformVector(tangent);
        spec.Tangents.push_back(world.SqrMagnitude() > 1e-12 ? world.Normalized() : Vec3d{});
    }
    return spec;
}

// New standalone brushes author local space at their AABB minimum, so the
// entity origin sits on the geometry instead of at world zero.
std::pair<Transform3f, BrushMesh> RebaseMeshToBoundsMin(BrushMesh mesh)
{
    Transform3f transform = Transform3f::Identity();
    if (mesh.Vertices.empty())
        return { transform, std::move(mesh) };

    Vec3d boundsMin = mesh.Vertices.front().Position;
    for (const BrushVertex& vertex : mesh.Vertices)
        for (int a = 0; a < 3; ++a)
            boundsMin[a] = std::min(boundsMin[a], vertex.Position[a]);
    for (BrushVertex& vertex : mesh.Vertices)
        vertex.Position -= boundsMin;
    transform.Position = boundsMin;
    return { transform, std::move(mesh) };
}

} // namespace

bool PendingBridgeEdit::SpansTwoBrushes(std::span<const SelectableRef> selection)
{
    return GroupEdgesByBrush(selection).size() == 2;
}

void PendingBridgeEdit::Begin(EditorScene& scene, EditorDocument& document, CommandStack& commands,
                              std::span<const SelectableRef> selection, int segments)
{
    Cancel(commands); // a re-bridge replaces the previous preview

    const auto groups = GroupEdgesByBrush(selection);
    if (groups.size() != 2)
        return;

    const std::optional<SelectedEdgePath> first =
        MakeSelectedEdgePath(scene, groups[0].first, groups[0].second);
    const std::optional<SelectedEdgePath> second =
        MakeSelectedEdgePath(scene, groups[1].first, groups[1].second);
    if (!first.has_value() || !second.has_value() || first->Closed != second->Closed
        || first->Vertices.size() != second->Vertices.size())
        return;

    BrushOps::BridgePathSpec pathA = MakeBridgePathSpec(scene, *first);
    BrushOps::BridgePathSpec pathB = MakeBridgePathSpec(scene, *second);
    FaceMaterial material = first->Material;
    const int clamped = std::clamp(segments, 1, 64);

    BrushMesh mesh = BrushOps::BuildBridgeBetweenPaths(pathA, pathB, clamped, &material);
    if (mesh.Faces.empty())
        return;

    auto [transform, local] = RebaseMeshToBoundsMin(std::move(mesh));

    Stage = Phase::Pending;
    Entity = scene.CreateBrushFromMesh(transform, std::move(local));
    PathA = std::move(pathA);
    PathB = std::move(pathB);
    Material = std::move(material);
    Segments = clamped;
    Scene = &scene;
    Document = &document;
    BeforeSelection.assign(selection.begin(), selection.end());

    commands.OpenPendingEdit([this, &commands] { Cancel(commands); });
}

void PendingBridgeEdit::SetSegments(int segments)
{
    if (Stage != Phase::Pending)
        return;
    segments = std::clamp(segments, 1, 64);
    if (segments == Segments)
        return;
    Segments = segments;
    Regenerate();
}

void PendingBridgeEdit::Regenerate()
{
    if (Stage != Phase::Pending)
        return;
    BrushMesh mesh = BrushOps::BuildBridgeBetweenPaths(PathA, PathB, Segments, &Material);
    if (mesh.Faces.empty())
        return; // keep the previous preview; the paths have not changed, so this cannot regress
    auto [transform, local] = RebaseMeshToBoundsMin(std::move(mesh));
    Scene->SetTransform(Entity, transform);
    Scene->SetBrushMesh(Entity, std::move(local));
}

void PendingBridgeEdit::Commit(CommandStack& commands, SelectionService& selection)
{
    if (Stage != Phase::Pending)
        return;

    EditorScene& scene = *Scene;
    EditorDocument& document = *Document;
    const EntityId entity = Entity;
    if (!entity.IsValid() || scene.TryGetBrushMesh(entity) == nullptr)
    {
        Cancel(commands);
        return;
    }

    EntitySnapshot snapshot = document.CaptureEntity(entity);
    std::vector<SelectableRef> beforeSelection = std::move(BeforeSelection);

    commands.ClosePendingEdit();
    Stage = Phase::Idle;
    ClearCapture();

    commands.Execute(std::make_unique<DeferredCreateBrushCommand>(
        entity, std::move(snapshot), scene, document, selection, std::move(beforeSelection)));
}

void PendingBridgeEdit::Cancel(CommandStack& commands)
{
    if (Stage != Phase::Pending)
        return;
    commands.ClosePendingEdit();
    Stage = Phase::Idle;
    if (Entity.IsValid() && Scene != nullptr)
        Scene->DestroyEntity(Entity);
    ClearCapture();
}

void PendingBridgeEdit::ClearCapture()
{
    Entity = {};
    PathA = {};
    PathB = {};
    Material = {};
    Segments = 1;
    Scene = nullptr;
    Document = nullptr;
    BeforeSelection.clear();
}
