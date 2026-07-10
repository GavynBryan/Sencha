#include "EditorWorkspace.h"

#include "EscapePolicy.h"

#include "EditorTheme.h"
#include "brush/BrushBounds.h"
#include "brush/BrushOps.h"
#include "document/EditorScene.h"
#include "document/commands/DeleteEntityCommand.h"
#include "document/commands/BreakInstanceCommand.h"
#include "document/commands/DeferredCreateBrushCommand.h"
#include "document/commands/DuplicateEntitiesCommand.h"
#include "document/commands/MergeBrushesCommand.h"
#include "document/commands/SeparateFacesCommand.h"
#include "commands/CompositeCommand.h"
#include "document/commands/SetBrushOriginCommand.h"
#include "document/tools/BrushTool.h"
#include "document/tools/EdgeCutTool.h"
#include "document/tools/FaceCarveTool.h"
#include "document/tools/SelectTool.h"
#include "meshedit/ElementGeometry.h"
#include "meshedit/MeshElements.h"
#include "meshedit/SelectionConversion.h"
#include "overlay/SelectionLabels.h"
#include "viewport/GridFrame.h"
#include "selection/SelectionFold.h"
#include "selection/commands/SelectCommand.h"

#include <algorithm>
#include <array>
#include <limits>
#include <map>

#include <math/geometry/3d/Aabb3d.h>

#include <memory>
#include <span>
#include <utility>
#include <vector>

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
}

EditorWorkspace::EditorWorkspace(LoggingProvider& logging)
    : World(logging)
    , Selection(LevelSelection)
    , MeshEdit(logging)
{
    World.BindViewSettings(&WorldView);
}

void EditorWorkspace::BuildInteractionState()
{
    EditorDocument& document = ActiveDocument();

    // Only the focus document is selectable (context zones are unpickable by
    // construction); the guard turns any stray cross-registry ref into a loud
    // debug failure instead of a silent stale selection.
    LevelSelection.SetExpectedRegistry(document.GetRegistry().Id);

    // All scene mutation during manipulation goes through this one sink; the
    // session, manipulators, and the edge-cut tool stay scene-agnostic. Built first
    // so the tool context can hold it.
    Sink = std::make_unique<BrushManipulationSink>(document.GetScene(), document, *Commands, Selection);

    ActiveToolContext = std::make_unique<ToolContext>(
        *Commands,
        Selection,
        Picking,
        document.GetScene(),
        document,
        Interactions,
        Preview,
        MeshEdit,
        Marquee,
        Grid,
        BrushCreate,
        Overlay,
        *Sink,
        EdgeCut,
        ActiveMaterial);

    Tools = std::make_unique<ToolRegistry>(*ActiveToolContext);
    Tools->Register(std::make_unique<SelectTool>());
    Tools->Register(std::make_unique<BrushTool>());
    Tools->Register(std::make_unique<EdgeCutTool>());
    Tools->Register(std::make_unique<FaceCarveTool>());
    Tools->Activate("select");

    // Lets a tool hand off to another (the edge cut switches to Select after a cut).
    ActiveToolContext->ActivateTool = [this](std::string_view id) { Tools->Activate(id); };

    Dispatcher = std::make_unique<ViewportToolDispatcher>(
        Layout,
        *ActiveToolContext,
        Interactions,
        Sessions,
        *Tools);

    // The session reads selection and element mode live on each pointer-down, so
    // it never needs rebuilding when the selection or mode changes. It consumes a
    // click only when a manipulator is hit; otherwise the select tool picks.
    auto session = std::make_unique<ManipulatorSession>(Selection, MeshEdit, *Sink, Grid, Pivot);
    Manipulators = session.get();
    Sessions.SetSession(std::move(session));

    // Resize quietly yields to Move while the selection has nothing resizable.
    Manipulators->SetResizableQuery(
        [this]
        {
            const EditorScene& scene = ActiveDocument().GetScene();
            for (const SelectableRef& ref : Selection.GetSelection())
                if (ref.IsEntity() && scene.TryGetBrushMesh(ref.Entity) != nullptr)
                    return true;
            return false;
        });
}

void EditorWorkspace::ResetInteractionState()
{
    if (Tools != nullptr)
        Tools->Cancel();
    // The pending bridge preview lives in whichever scene it was begun in; a
    // focus change keeps that document alive, so cancel (not just drop) it.
    CancelPendingBridge();
    if (Commands != nullptr)
        Commands->Clear();
    Selection.ClearSelection();

    // Transient view/interaction state that may reference the outgoing document.
    Marquee = {};
    Pivot = {};
    Overlay.Labels.clear();
    Overlay.PointHandles.clear();
    Overlay.Readout.Clear();
    Overlay.Hover = {};
    Overlay.HoverBody = {};
    Preview.Clear();

    BuildInteractionState();
}

void EditorWorkspace::Init(CommandStack& commands)
{
    Commands = &commands;

    BuildInteractionState();

    // A focus change swaps the edited document: reset exactly as document open
    // does. In legacy mode focus never changes, so this never fires.
    World.OnFocusChanged = [this] { ResetInteractionState(); };

    // An unload destroys a zone document that queued commands may still
    // reference (the cross-zone move holds two documents). Focus is unchanged,
    // so only the stack drops; tools and selection stay.
    World.OnZoneUnloaded = [this](ZoneId)
    {
        if (Commands != nullptr)
            Commands->Clear();
    };

    // The transient pivot is per-selection: any selection change resets it to
    // the computed center AND leaves pivot-editing (clicking another object
    // means the user is done placing this pivot).
    PivotObserver = Selection.Subscribe(
        [this](const SelectionSnapshot&)
        {
            Pivot.Override.reset();
            Pivot.Editing = false;
        });

    // A selection of plain entities only (no brush, no element refs) cannot be
    // edited in a mesh-element mode: drop back to Object so the new selection is
    // immediately workable. Runs inside selection Notify; it must not mutate the
    // selection (SetElementKind only touches mode state).
    ModeObserver = Selection.Subscribe(
        [this](const SelectionSnapshot& snapshot)
        {
            if (snapshot.Items.empty() || MeshEdit.GetElementKind() == MeshElementKind::Object)
                return;
            const EditorScene& scene = ActiveDocument().GetScene();
            for (const SelectableRef& ref : snapshot.Items)
                if (ref.IsMeshElement() || (ref.IsEntity() && scene.TryGetBrushMesh(ref.Entity) != nullptr))
                    return;
            MeshEdit.SetElementKind(MeshElementKind::Object);
        });

    // Element-kind changes carry the selection into the new kind, then restore
    // the gizmo last used in the entered context. Resolves this->Manipulators at
    // fire time, so it survives session rebuilds.
    MeshEdit.SetElementKindObserver([this](MeshElementKind next)
    {
        ConvertSelectionToKind(next);
        Manipulators->OnElementKindChanged(next);
    });
}

void EditorWorkspace::ConvertSelectionToKind(MeshElementKind next)
{
    const SelectionSnapshot current = Selection.GetSnapshot();
    const EditorScene& scene = ActiveDocument().GetScene();

    std::vector<SelectableRef> converted;
    converted.reserve(current.Items.size());
    bool anyConverted = false;
    const auto appendUnique = [&](SelectableRef ref)
    {
        if (std::find(converted.begin(), converted.end(), ref) == converted.end())
            converted.push_back(ref);
    };

    for (const SelectableRef& ref : current.Items)
    {
        const BrushMesh* mesh = ref.IsMeshElement() ? scene.TryGetBrushMesh(ref.Entity) : nullptr;
        if (mesh == nullptr)
        {
            appendUnique(ref);
            continue;
        }
        for (SelectableRef out : ConvertElementRefs(*mesh, std::span(&ref, 1), next))
        {
            anyConverted |= out != ref;
            appendUnique(out);
        }
    }

    // Only touch the selection when something actually changed kind: the
    // plain-entity fallback to Object fires from inside a selection
    // notification, and re-notifying there would recurse.
    if (!anyConverted)
        return;

    SelectionSnapshot snapshot;
    snapshot.Items = std::move(converted);
    snapshot.Primary = snapshot.Items.empty() ? SelectableRef{} : snapshot.Items.back();
    Selection.ApplySnapshot(std::move(snapshot));
}

void EditorWorkspace::SelectAll()
{
    if (Commands == nullptr)
        return;

    const EditorScene& scene = ActiveDocument().GetScene();
    const MeshElementKind kind = MeshEdit.GetElementKind();
    const RegistryId registry = scene.GetRegistry().Id;

    std::vector<SelectableRef> gathered;
    if (kind == MeshElementKind::Object)
    {
        for (EntityId entity : scene.GetAllEntities())
            if (scene.IsEntityVisible(entity) && !scene.IsEntityLocked(entity))
                gathered.push_back(SelectableRef::EntitySelection(registry, entity));
    }
    else
    {
        // Every element of the current kind on each brush the selection touches
        // (entity refs and element refs both name their entity).
        std::vector<EntityId> entities;
        for (const SelectableRef& ref : Selection.GetSelection())
            if (ref.Entity.IsValid() && std::find(entities.begin(), entities.end(), ref.Entity) == entities.end())
                entities.push_back(ref.Entity);
        for (EntityId entity : entities)
        {
            const BrushMesh* mesh = scene.TryGetBrushMesh(entity);
            const Transform3f* transform = scene.TryGetTransform(entity);
            if (mesh == nullptr || transform == nullptr)
                continue;
            std::vector<SelectableRef> refs = MeshElements::AllRefs(*mesh, *transform, registry, entity, kind);
            gathered.insert(gathered.end(), refs.begin(), refs.end());
        }
    }

    if (gathered.empty())
        return;

    SelectionSnapshot snapshot = SelectionFold::Apply({}, gathered, SelectionFold::Op::Replace);
    Commands->Execute(std::make_unique<SelectCommand>(Selection, std::move(snapshot)));
}

void EditorWorkspace::DuplicateSelection(bool asInstance)
{
    if (Commands == nullptr)
        return;

    const EditorScene& scene = ActiveDocument().GetScene();
    std::vector<EntityId> sources;
    std::vector<Transform3f> transforms;
    for (const SelectableRef& ref : Selection.GetSelection())
    {
        if (!ref.IsEntity() || !scene.HasEntity(ref.Entity))
            continue;
        sources.push_back(ref.Entity);
        const Transform3f* transform = scene.TryGetTransform(ref.Entity);
        transforms.push_back(transform != nullptr ? *transform : Transform3f::Identity());
    }
    if (sources.empty())
        return;

    Commands->Execute(std::make_unique<DuplicateEntitiesCommand>(
        sources, transforms, ActiveDocument().GetScene(), ActiveDocument(), Selection, asInstance));
}

void EditorWorkspace::MakeSelectedBrushesUnique()
{
    if (Commands == nullptr)
        return;

    EditorScene& scene = ActiveDocument().GetScene();
    std::vector<std::unique_ptr<ICommand>> commands;
    for (const SelectableRef& ref : Selection.GetSelection())
        if (ref.IsEntity())
            if (auto command = MakeBreakInstanceCommand(scene, ActiveDocument(), ref.Entity))
                commands.push_back(std::move(command));

    if (commands.empty())
        return;
    if (commands.size() == 1)
        Commands->Execute(std::move(commands.front()));
    else
        Commands->Execute(std::make_unique<CompositeCommand>(std::move(commands)));
}

void EditorWorkspace::MergeSelectedBrushes()
{
    if (Commands == nullptr)
        return;

    EditorScene& scene = ActiveDocument().GetScene();
    const SelectableRef primary = Selection.GetPrimarySelection();
    EntityId target = primary.IsEntity() && scene.TryGetBrushMesh(primary.Entity) != nullptr
        ? primary.Entity
        : EntityId{};
    std::vector<EntityId> sources;
    for (const SelectableRef& ref : Selection.GetSelection())
    {
        if (!ref.IsEntity() || scene.TryGetBrushMesh(ref.Entity) == nullptr)
            continue;
        if (!target.IsValid())
        {
            target = ref.Entity;
            continue;
        }
        if (ref.Entity != target)
            sources.push_back(ref.Entity);
    }

    if (auto command = MakeMergeBrushesCommand(target, sources, scene, ActiveDocument(), Selection))
        Commands->Execute(std::move(command));
}

void EditorWorkspace::SeparateSelectedFaces()
{
    if (Commands == nullptr)
        return;

    // Face refs of ONE brush (the mesh verbs' contract); the first entity with
    // face refs wins.
    EntityId source;
    std::vector<std::uint32_t> faces;
    for (const SelectableRef& ref : Selection.GetSelection())
    {
        if (!ref.IsFace())
            continue;
        if (!source.IsValid())
            source = ref.Entity;
        if (ref.Entity == source)
            faces.push_back(ref.ElementId);
    }

    if (auto command = MakeSeparateFacesCommand(source, faces, ActiveDocument().GetScene(), ActiveDocument(), Selection))
    {
        Commands->Execute(std::move(command));
        // The face indices no longer resolve on the reshaped source.
        Selection.ClearMeshElementSelections();
        MeshEdit.SetElementKind(MeshElementKind::Object);
    }
}

void EditorWorkspace::SyncOrthoViewsToGridFrame()
{
    Vec3d u;
    Vec3d n;
    Vec3d v;
    GridFrame::Basis(Grid, u, n, v);

    for (const auto& viewport : Layout.All())
    {
        const OrientationTraits& traits = viewport->GetOrientationTraits();
        if (traits.Mode != EditorCamera::Mode::Orthographic || traits.UsesCameraAxis)
            continue;

        viewport->Camera.OrthoAxis = GridFrame::MapToFrame(traits.OrthoAxis, u, n, v);
        // The same view-up rule the world-aligned basis uses (world up, or
        // forward when looking straight down/up), expressed in the frame.
        const Vec3d upDefault = std::abs(traits.OrthoAxis.Y) > 0.999f ? Vec3d::Forward() : Vec3d::Up();
        viewport->Camera.OrthoUpHint = GridFrame::MapToFrame(upDefault, u, n, v);
    }
}

void EditorWorkspace::SetGridOriginToSelection()
{
    const EditorScene& scene = ActiveDocument().GetScene();

    // A single selected vertex is the exact intent; use its world position.
    const SelectableRef* vertexRef = nullptr;
    for (const SelectableRef& ref : Selection.GetSelection())
    {
        if (!ref.IsVertex())
            continue;
        if (vertexRef != nullptr)
        {
            vertexRef = nullptr;
            break;
        }
        vertexRef = &ref;
    }
    if (vertexRef != nullptr)
    {
        const BrushMesh* mesh = scene.TryGetBrushMesh(vertexRef->Entity);
        const Transform3f* transform = scene.TryGetTransform(vertexRef->Entity);
        if (mesh != nullptr && transform != nullptr)
        {
            if (const auto vertex = MeshElements::TryGetVertex(*mesh, *transform, vertexRef->ElementId))
            {
                Grid.Origin = vertex->Position;
                return;
            }
        }
    }

    Aabb3d bounds = Aabb3d::Empty();
    for (const SelectableRef& ref : Selection.GetSelection())
    {
        if (!ref.Entity.IsValid())
            continue;
        if (const auto entityBounds = scene.TryGetWorldBounds(ref.Entity))
            bounds.ExpandToInclude(*entityBounds);
    }
    if (bounds.IsValid())
        Grid.Origin = bounds.Center();
}

void EditorWorkspace::AlignGridToSelectedFace()
{
    const EditorScene& scene = ActiveDocument().GetScene();

    SelectableRef faceRef = Selection.GetPrimarySelection();
    if (!faceRef.IsFace())
    {
        faceRef = {};
        for (const SelectableRef& ref : Selection.GetSelection())
            if (ref.IsFace())
            {
                faceRef = ref;
                break;
            }
    }
    if (!faceRef.IsFace())
        return;

    const BrushMesh* mesh = scene.TryGetBrushMesh(faceRef.Entity);
    const Transform3f* transform = scene.TryGetTransform(faceRef.Entity);
    if (mesh == nullptr || transform == nullptr)
        return;

    const auto face = MeshElements::TryGetFace(*mesh, *transform, faceRef.ElementId);
    if (!face.has_value())
        return;

    (void)GridFrame::FromFace(face->Center, face->Normal,
                              GridFrame::LongestEdgeDirection(face->Corners), Grid);
}

void EditorWorkspace::RotateGridInPlane(float degrees)
{
    GridFrame::RotateInPlane(Grid, degrees);
}

void EditorWorkspace::ResetGrid()
{
    Grid.ResetFrame();
}

void EditorWorkspace::EscapeStep()
{
    if (Commands == nullptr)
        return;

    bool hasElementRefs = false;
    for (const SelectableRef& ref : Selection.GetSelection())
        if (ref.IsMeshElement())
        {
            hasElementRefs = true;
            break;
        }

    switch (NextEscapeAction(Manipulators->IsEditingGridOrigin(), Pivot.Editing, hasElementRefs,
                             MeshEdit.GetElementKind(), !Selection.GetSelection().empty()))
    {
    case EscapeAction::CancelGridOriginEdit:
        Manipulators->SetEditingGridOrigin(false);
        break;
    case EscapeAction::CancelPivotEdit:
        Manipulators->SetEditingPivot(false);
        break;
    case EscapeAction::ClearElementSelection:
    {
        SelectionSnapshot entitiesOnly;
        for (const SelectableRef& ref : Selection.GetSelection())
            if (ref.IsEntity())
                entitiesOnly.Items.push_back(ref);
        entitiesOnly.Primary = entitiesOnly.Items.empty() ? SelectableRef{} : entitiesOnly.Items.back();
        Commands->Execute(std::make_unique<SelectCommand>(Selection, std::move(entitiesOnly)));
        break;
    }
    case EscapeAction::DropToObjectMode:
        MeshEdit.SetElementKind(MeshElementKind::Object);
        break;
    case EscapeAction::ClearSelection:
        Commands->Execute(std::make_unique<SelectCommand>(Selection, SelectionSnapshot{}));
        break;
    case EscapeAction::None:
        break;
    }
}

void EditorWorkspace::UpdateOverlay()
{
    Overlay.Labels.clear();

    // Union the world bounds of every selected brush, so the dimension labels
    // describe the selection's extents as one box.
    const EditorScene& scene = ActiveDocument().GetScene();
    Aabb3d bounds = Aabb3d::Empty();
    for (const SelectableRef& ref : Selection.GetSelection())
    {
        if (!ref.IsEntity())
            continue;
        const BrushMesh* mesh = scene.TryGetBrushMesh(ref.Entity);
        const Transform3f* transform = scene.TryGetTransform(ref.Entity);
        if (mesh == nullptr || transform == nullptr)
            continue;
        const Aabb3d entityBounds = BrushWorldBounds(*mesh, *transform);
        if (entityBounds.IsValid())
            bounds.ExpandToInclude(entityBounds);
    }

    if (bounds.IsValid())
        Overlay.Labels = SelectionDimensionLabels(bounds, EditorTheme::DimensionLabel);

    // Zone name labels ride the same per-frame label rebuild as the dimension
    // text, anchored at each zone box's top center.
    if (World.IsWorld() && WorldView.ShowZoneBounds)
    {
        for (const ZoneHeader& zone : World.Manifest().Zones)
        {
            if (!zone.Bounds.IsValid())
                continue;
            const Vec3d center = zone.Bounds.Center();
            LabelRequest label;
            label.World = Vec3d(center.X, zone.Bounds.Max.Y, center.Z);
            label.Color = World.FocusZone() == zone.Id ? EditorTheme::Selection
                                                       : EditorTheme::BoundsBox;
            label.Text = zone.Name;
            Overlay.Labels.push_back(std::move(label));
        }
    }
}

void EditorWorkspace::SetSelectedBrushOriginToPivot()
{
    if (Commands == nullptr || !Pivot.Override.has_value())
        return;

    const SelectableRef primary = Selection.GetPrimarySelection();
    if (!primary.IsEntity())
        return;

    if (auto command = MakeSetBrushOriginCommand(ActiveDocument().GetScene(), primary.Entity, *Pivot.Override))
    {
        Commands->Execute(std::move(command));
        Pivot.Override.reset(); // the origin is now the pivot; drop the transient
    }
}

void EditorWorkspace::SetSelectedBrushOrigin(OriginAnchor anchor)
{
    if (Commands == nullptr)
        return;

    EntityId entity = {};
    std::optional<Vec3d> point;

    if (anchor == OriginAnchor::SelectedVertex)
    {
        // The first selected vertex wins; the origin lands exactly on it.
        if (Sink == nullptr)
            return;
        for (const SelectableRef& ref : Selection.GetSelection())
        {
            if (!ref.IsVertex())
                continue;
            const std::optional<MeshEditTargetMesh> resolved = Sink->ResolveMesh(ref.Entity);
            if (!resolved.has_value() || resolved->Mesh == nullptr)
                continue;
            point = ElementCenter(*resolved->Mesh, resolved->Transform, ref);
            entity = ref.Entity;
            break;
        }
    }
    else
    {
        entity = Selection.GetPrimarySelection().Entity;
        if (const std::optional<Aabb3d> bounds = ActiveDocument().GetScene().TryGetWorldBounds(entity))
            point = anchor == OriginAnchor::BoundsCenter ? bounds->Center() : bounds->Min;
    }

    if (!point.has_value())
        return;

    if (auto command = MakeSetBrushOriginCommand(ActiveDocument().GetScene(), entity, *point))
    {
        Commands->Execute(std::move(command));
        Pivot.Override.reset(); // the pivot tracked the old origin; drop the transient
    }
}

void EditorWorkspace::ApplyActiveMaterialToSelectedFaces()
{
    if (!ActiveMaterial.Active.IsValid() || Sink == nullptr || Commands == nullptr)
        return;
    ApplyMaterialToSelectedFaces(*Sink, Selection, *Commands, ActiveMaterial.Active);
}

void EditorWorkspace::CopySelectedFaceProjection()
{
    if (Sink == nullptr)
        return;
    if (auto copied = ::CopySelectedFaceProjection(*Sink, Selection))
        UvClipboard = std::move(copied);
}

void EditorWorkspace::PasteFaceProjectionToSelection()
{
    if (!UvClipboard.has_value() || Sink == nullptr || Commands == nullptr)
        return;
    PasteFaceProjection(*Sink, Selection, *Commands, *UvClipboard);
}

void EditorWorkspace::DeleteSelection()
{
    if (Commands == nullptr)
        return;

    // Element selections delete through mesh verbs: faces are removed, edges
    // dissolve (merge their two faces). Vertex refs have no delete verb yet.
    bool hasFace = false;
    bool hasEdge = false;
    for (const SelectableRef& ref : Selection.GetSelection())
    {
        hasFace |= ref.IsFace();
        hasEdge |= ref.IsEdge();
    }
    if (Sink != nullptr && (hasFace || hasEdge))
    {
        const MeshEditVerb verb = hasFace ? MeshEditVerb::Delete : MeshEditVerb::DissolveEdge;
        if (auto command = MeshEdit.ApplyVerb(*Sink, Selection.GetSnapshot(), verb, {}))
        {
            Commands->Execute(std::move(command));
            Selection.ClearMeshElementSelections();
        }
        return;
    }

    // Entity-kind selections only; vertex/edge/face element refs are not entities.
    std::vector<EntityId> entities;
    for (const SelectableRef& ref : Selection.GetSelection())
        if (ref.Kind == SelectableKind::Entity && ref.Entity.IsValid())
            entities.push_back(ref.Entity);

    if (entities.empty())
        return;

    Commands->Execute(MakeDeleteEntitiesCommand(entities, ActiveDocument().GetScene(), ActiveDocument(), Selection));
}

void EditorWorkspace::DissolveSelectedEdges()
{
    if (Commands == nullptr || Sink == nullptr)
        return;
    if (auto command = MeshEdit.ApplyVerb(*Sink, Selection.GetSnapshot(), MeshEditVerb::DissolveEdge, {}))
    {
        Commands->Execute(std::move(command));
        Selection.ClearMeshElementSelections();
    }
}

void EditorWorkspace::BridgeSelectedEdges(int segments)
{
    if (Commands == nullptr || Sink == nullptr)
        return;

    const SelectionSnapshot selection = Selection.GetSnapshot();
    std::vector<std::pair<EntityId, std::vector<SelectableRef>>> groups;
    for (const SelectableRef& ref : selection.Items)
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

    if (groups.size() != 2)
    {
        if (auto command = MeshEdit.ApplyVerb(*Sink, selection, MeshEditVerb::BridgeEdges,
                                              { .BridgeSegments = segments }))
        {
            Commands->Execute(std::move(command));
            Selection.ClearMeshElementSelections();
        }
        return;
    }

    EditorScene& scene = ActiveDocument().GetScene();
    std::optional<SelectedEdgePath> first = MakeSelectedEdgePath(scene, groups[0].first, groups[0].second);
    std::optional<SelectedEdgePath> second = MakeSelectedEdgePath(scene, groups[1].first, groups[1].second);
    if (!first.has_value() || !second.has_value() || first->Closed != second->Closed
        || first->Vertices.size() != second->Vertices.size())
        return;

    PendingBridge pending;
    pending.PathA = MakeBridgePathSpec(scene, *first);
    pending.PathB = MakeBridgePathSpec(scene, *second);
    pending.Material = first->Material;
    pending.Segments = std::clamp(segments, 1, 64);
    pending.Scene = &scene;
    pending.Document = &ActiveDocument();
    pending.BeforeSelection = selection.Items;
    BeginPendingBridge(std::move(pending));
}

void EditorWorkspace::BeginPendingBridge(PendingBridge pending)
{
    CancelPendingBridge(); // a re-bridge replaces the previous preview

    BrushMesh mesh = BrushOps::BuildBridgeBetweenPaths(pending.PathA, pending.PathB,
                                                       pending.Segments, &pending.Material);
    if (mesh.Faces.empty())
        return;

    auto [transform, local] = RebaseMeshToBoundsMin(std::move(mesh));
    pending.Entity = pending.Scene->CreateBrushFromMesh(transform, std::move(local));

    Bridge = BridgeState::Pending;
    PendingBridgeData = std::move(pending);
    Commands->OpenPendingEdit([this] { CancelPendingBridge(); });
}

void EditorWorkspace::RegeneratePendingBridge()
{
    if (Bridge != BridgeState::Pending)
        return;
    BrushMesh mesh = BrushOps::BuildBridgeBetweenPaths(PendingBridgeData.PathA, PendingBridgeData.PathB,
                                                       PendingBridgeData.Segments,
                                                       &PendingBridgeData.Material);
    if (mesh.Faces.empty())
        return; // keep the previous preview; the paths have not changed, so this cannot regress
    auto [transform, local] = RebaseMeshToBoundsMin(std::move(mesh));
    PendingBridgeData.Scene->SetTransform(PendingBridgeData.Entity, transform);
    PendingBridgeData.Scene->SetBrushMesh(PendingBridgeData.Entity, std::move(local));
}

void EditorWorkspace::SetPendingBridgeSegments(int segments)
{
    if (Bridge != BridgeState::Pending)
        return;
    segments = std::clamp(segments, 1, 64);
    if (segments == PendingBridgeData.Segments)
        return;
    PendingBridgeData.Segments = segments;
    RegeneratePendingBridge();
}

void EditorWorkspace::CommitPendingBridge()
{
    if (Bridge != BridgeState::Pending)
        return;
    EditorScene& scene = *PendingBridgeData.Scene;
    const EntityId entity = PendingBridgeData.Entity;
    if (!entity.IsValid() || scene.TryGetBrushMesh(entity) == nullptr)
    {
        CancelPendingBridge();
        return;
    }

    EntitySnapshot snapshot = PendingBridgeData.Document->CaptureEntity(entity);
    std::vector<SelectableRef> beforeSelection = std::move(PendingBridgeData.BeforeSelection);
    EditorDocument& document = *PendingBridgeData.Document;
    Commands->ClosePendingEdit();
    Bridge = BridgeState::Idle;
    PendingBridgeData = {};
    Commands->Execute(std::make_unique<DeferredCreateBrushCommand>(
        entity, std::move(snapshot), scene, document, Selection, std::move(beforeSelection)));
}

void EditorWorkspace::CancelPendingBridge()
{
    if (Bridge != BridgeState::Pending)
        return;
    Commands->ClosePendingEdit();
    Bridge = BridgeState::Idle;
    if (PendingBridgeData.Entity.IsValid() && PendingBridgeData.Scene != nullptr)
        PendingBridgeData.Scene->DestroyEntity(PendingBridgeData.Entity);
    PendingBridgeData = {};
}
