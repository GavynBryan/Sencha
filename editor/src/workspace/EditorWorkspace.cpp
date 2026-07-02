#include "EditorWorkspace.h"

#include "EscapePolicy.h"

#include "../EditorTheme.h"
#include "../brush/BrushBounds.h"
#include "../document/EditorScene.h"
#include "../document/commands/DeleteEntityCommand.h"
#include "../document/commands/BreakInstanceCommand.h"
#include "../document/commands/DuplicateEntitiesCommand.h"
#include "../document/commands/MergeBrushesCommand.h"
#include "../document/commands/SeparateFacesCommand.h"
#include "../commands/CompositeCommand.h"
#include "../document/commands/SetBrushOriginCommand.h"
#include "../document/tools/BrushTool.h"
#include "../document/tools/EdgeCutTool.h"
#include "../document/tools/FaceCarveTool.h"
#include "../document/tools/SelectTool.h"
#include "../meshedit/MeshElements.h"
#include "../overlay/SelectionLabels.h"
#include "../viewport/GridFrame.h"
#include "../selection/SelectionFold.h"
#include "../selection/commands/SelectCommand.h"

#include <algorithm>

#include <math/geometry/3d/Aabb3d.h>

#include <memory>
#include <utility>
#include <vector>

EditorWorkspace::EditorWorkspace(LoggingProvider& logging)
    : Document(logging)
    , Selection(LevelSelection)
    , MeshEdit(logging)
{
}

void EditorWorkspace::Init(CommandStack& commands)
{
    Commands = &commands;

    // All scene mutation during manipulation goes through this one sink; the
    // session, manipulators, and the edge-cut tool stay scene-agnostic. Built first
    // so the tool context can hold it.
    Sink = std::make_unique<BrushManipulationSink>(Document.GetScene(), Document, commands, Selection);

    ActiveToolContext = std::make_unique<ToolContext>(
        commands,
        Selection,
        Picking,
        Document.GetScene(),
        Document,
        Interactions,
        Preview,
        MeshEdit,
        Marquee,
        Grid,
        BrushCreate,
        Overlay,
        *Sink,
        EdgeCut);

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
            const EditorScene& scene = Document.GetScene();
            for (const SelectableRef& ref : snapshot.Items)
                if (ref.IsMeshElement() || (ref.IsEntity() && scene.TryGetBrushMesh(ref.Entity) != nullptr))
                    return;
            MeshEdit.SetElementKind(MeshElementKind::Object);
        });

    // Element-kind changes restore the gizmo last used in the entered context, and
    // Resize quietly yields to Move while the selection has nothing resizable.
    MeshEdit.SetElementKindObserver([this](MeshElementKind next) { Manipulators->OnElementKindChanged(next); });
    Manipulators->SetResizableQuery(
        [this]
        {
            const EditorScene& scene = Document.GetScene();
            for (const SelectableRef& ref : Selection.GetSelection())
                if (ref.IsEntity() && scene.TryGetBrushMesh(ref.Entity) != nullptr)
                    return true;
            return false;
        });
}

void EditorWorkspace::SelectAll()
{
    if (Commands == nullptr)
        return;

    const EditorScene& scene = Document.GetScene();
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

    const EditorScene& scene = Document.GetScene();
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
        sources, transforms, Document.GetScene(), Document, Selection, asInstance));
}

void EditorWorkspace::MakeSelectedBrushesUnique()
{
    if (Commands == nullptr)
        return;

    EditorScene& scene = Document.GetScene();
    std::vector<std::unique_ptr<ICommand>> commands;
    for (const SelectableRef& ref : Selection.GetSelection())
        if (ref.IsEntity())
            if (auto command = MakeBreakInstanceCommand(scene, Document, ref.Entity))
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

    EditorScene& scene = Document.GetScene();
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

    if (auto command = MakeMergeBrushesCommand(target, sources, scene, Document, Selection))
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

    if (auto command = MakeSeparateFacesCommand(source, faces, Document.GetScene(), Document, Selection))
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
    const EditorScene& scene = Document.GetScene();

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
    const EditorScene& scene = Document.GetScene();

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
    const EditorScene& scene = Document.GetScene();
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
}

void EditorWorkspace::SetSelectedBrushOriginToPivot()
{
    if (Commands == nullptr || !Pivot.Override.has_value())
        return;

    const SelectableRef primary = Selection.GetPrimarySelection();
    if (!primary.IsEntity())
        return;

    if (auto command = MakeSetBrushOriginCommand(Document.GetScene(), primary.Entity, *Pivot.Override))
    {
        Commands->Execute(std::move(command));
        Pivot.Override.reset(); // the origin is now the pivot; drop the transient
    }
}

void EditorWorkspace::DeleteSelection()
{
    if (Commands == nullptr)
        return;

    // Entity-kind selections only; vertex/edge/face element refs are not entities.
    std::vector<EntityId> entities;
    for (const SelectableRef& ref : Selection.GetSelection())
        if (ref.Kind == SelectableKind::Entity && ref.Entity.IsValid())
            entities.push_back(ref.Entity);

    if (entities.empty())
        return;

    Commands->Execute(MakeDeleteEntitiesCommand(entities, Document.GetScene(), Document, Selection));
}
