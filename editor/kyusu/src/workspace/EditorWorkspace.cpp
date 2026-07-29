#include "EditorWorkspace.h"

#include "EscapePolicy.h"

#include "BrushManipulationSink.h"
#include "editmodes/ManipulatorSession.h"
#include "tools/ToolRegistry.h"

#include "EditorTheme.h"
#include "authoring/WorldDockEditorAdapter.h"
#include "brush/BrushBounds.h"
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
#include "GridEditing.h"
#include "viewport/GridFrame.h"
#include "selection/SelectionFold.h"
#include "selection/commands/SelectCommand.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>

#include <math/geometry/3d/Aabb3d.h>

#include <memory>
#include <span>
#include <utility>
#include <vector>

EditorWorkspace::EditorWorkspace(LoggingProvider& logging)
    : World(logging)
    , Selection(LevelSelection)
    , MeshEdit(logging)
{
    World.BindViewSettings(&WorldView);
}

void EditorWorkspace::BuildInteractionState()
{
    Interaction.Rebuild(
        WorkspaceInteractionInputs{
            World,
            LevelSelection,
            Selection,
            Picking,
            MeshEdit,
            Layout,
            Pivot,
            *Affordances,
            ToolAuthoringSettings{ Grid, BrushCreate, EdgeCut, ActiveMaterial },
            // A committed duplicate (the gizmo Shift-drag) becomes the repeatable
            // action: Ctrl+R re-duplicates the current selection at the same offset.
            [this](Vec3d offset)
            {
                if (offset.SqrMagnitude() > 0.0f)
                    LastRepeatable = [this, offset] { DuplicateSelectionWithOffset(offset); };
            },
        },
        *Commands);
}

void EditorWorkspace::ResetInteractionState()
{
    Interaction.CancelActiveTool();
    // The pending bridge preview lives in whichever scene it was begun in; a
    // focus change keeps that document alive, so cancel (not just drop) it.
    CancelPendingBridge();
    CancelPendingElementEdit();
    if (Commands != nullptr)
        Commands->Clear();
    Selection.ClearSelection();

    // Transient view/interaction state that may reference the outgoing document.
    Interaction.ClearTransient();
    Pivot = {};

    BuildInteractionState();
}

void EditorWorkspace::Init(CommandStack& commands)
{
    Commands = &commands;

    Affordances = std::make_unique<EditorAffordanceService>(
        World, Selection, commands, Grid);
    Affordances->Registry().Register(MakeWorldDockEditorAdapter());
    CreationRecipes.Register("world_dock", std::make_unique<WorldDockRecipe>());
    Picking.SetEntityProxyProvider(
        [this](const Ray3d& ray, const EditorScene& scene)
        { return Affordances->Pick(ray, scene); });

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

    ZoneSelectionObserver = Selection.Subscribe(
        [this](const SelectionSnapshot& snapshot)
        {
            if (!snapshot.Items.empty())
                (void)World.SelectZone(ZoneId{});
        });

    // Element-kind changes carry the selection into the new kind, then restore
    // the gizmo last used in the entered context. Resolves this->Manipulators at
    // fire time, so it survives session rebuilds.
    MeshEdit.SetElementKindObserver([this](MeshElementKind next)
    {
        ConvertSelectionToKind(next);
        Interaction.Manipulators->OnElementKindChanged(next);
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
        sources, transforms, ActiveDocument().GetScene(), ActiveDocument(), Selection,
        asInstance, [this](std::vector<EntitySnapshot>& snapshots)
        { RemapWorldConnectionDuplicateSnapshots(World, snapshots); }));
}

void EditorWorkspace::RepeatLastAction()
{
    if (LastRepeatable)
        LastRepeatable();
}

void EditorWorkspace::DuplicateSelectionWithOffset(Vec3d offset)
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
        Transform3f placed = transform != nullptr ? *transform : Transform3f::Identity();
        placed.Position += offset;
        transforms.push_back(placed);
    }
    if (sources.empty())
        return;

    Commands->Execute(std::make_unique<DuplicateEntitiesCommand>(
        sources, transforms, ActiveDocument().GetScene(), ActiveDocument(), Selection));
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
    GridEditing::SyncOrthoViews(Grid, Layout);
}

void EditorWorkspace::SetGridOriginToSelection()
{
    GridEditing::SetOriginToSelection(Grid, ActiveDocument().GetScene(), Selection.GetSelection());
}

void EditorWorkspace::AlignGridToSelectedFace()
{
    GridEditing::AlignToSelectedFace(Grid, ActiveDocument().GetScene(), Selection.GetSelection(),
                                     Selection.GetPrimarySelection());
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

    switch (NextEscapeAction(Interaction.Manipulators->IsEditingGridOrigin(), Pivot.Editing, hasElementRefs,
                             MeshEdit.GetElementKind(), !Selection.GetSelection().empty()))
    {
    case EscapeAction::CancelGridOriginEdit:
        Interaction.Manipulators->SetEditingGridOrigin(false);
        break;
    case EscapeAction::CancelPivotEdit:
        Interaction.Manipulators->SetEditingPivot(false);
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
    Interaction.Overlay.Labels.clear();

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
        Interaction.Overlay.Labels = SelectionDimensionLabels(bounds, EditorTheme::DimensionLabel);

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
            Interaction.Overlay.Labels.push_back(std::move(label));
        }
    }

    if (Affordances)
    {
        ViewportAffordanceOutput affordances;
        Affordances->Build(affordances);
        Interaction.Overlay.Labels.insert(Interaction.Overlay.Labels.end(),
                              std::make_move_iterator(affordances.Labels.begin()),
                              std::make_move_iterator(affordances.Labels.end()));
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
        if (Interaction.Sink == nullptr)
            return;
        for (const SelectableRef& ref : Selection.GetSelection())
        {
            if (!ref.IsVertex())
                continue;
            const std::optional<MeshEditTargetMesh> resolved = Interaction.Sink->ResolveMesh(ref.Entity);
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
        {
            if (anchor == OriginAnchor::BoundsCenter)
            {
                point = bounds->Center();
            }
            else
            {
                // The bounds min of freeform geometry rarely sits on a lattice
                // point; land the origin on the finest grid so the brush stays
                // grid-addressable afterwards.
                const auto snap = [](float x)
                { return std::round(x / GridSettings::kMinSpacing) * GridSettings::kMinSpacing; };
                point = Vec3d{ snap(bounds->Min.X), snap(bounds->Min.Y), snap(bounds->Min.Z) };
            }
        }
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
    if (!ActiveMaterial.Active.IsValid() || Interaction.Sink == nullptr || Commands == nullptr)
        return;
    ApplyMaterialToSelectedFaces(*Interaction.Sink, Selection, *Commands, ActiveMaterial.Active);
}

void EditorWorkspace::CopySelectedFaceProjection()
{
    if (Interaction.Sink == nullptr)
        return;
    if (auto copied = ::CopySelectedFaceProjection(*Interaction.Sink, Selection))
        UvClipboard = std::move(copied);
}

void EditorWorkspace::PasteFaceProjectionToSelection()
{
    if (!UvClipboard.has_value() || Interaction.Sink == nullptr || Commands == nullptr)
        return;
    PasteFaceProjection(*Interaction.Sink, Selection, *Commands, *UvClipboard);
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
    if (Interaction.Sink != nullptr && (hasFace || hasEdge))
    {
        const MeshEditVerb verb = hasFace ? MeshEditVerb::Delete : MeshEditVerb::DissolveEdge;
        if (auto command = MeshEdit.ApplyVerb(*Interaction.Sink, Selection.GetSnapshot(), verb, {}))
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
    if (Commands == nullptr || Interaction.Sink == nullptr)
        return;
    if (auto command = MeshEdit.ApplyVerb(*Interaction.Sink, Selection.GetSnapshot(), MeshEditVerb::DissolveEdge, {}))
    {
        Commands->Execute(std::move(command));
        Selection.ClearMeshElementSelections();
    }
}

void EditorWorkspace::BridgeSelectedEdges(int segments)
{
    if (Commands == nullptr || Interaction.Sink == nullptr)
        return;

    const SelectionSnapshot selection = Selection.GetSnapshot();
    if (PendingBridgeEdit::SpansTwoBrushes(selection.Items))
    {
        BridgeEdit.Begin(ActiveDocument().GetScene(), ActiveDocument(), *Commands,
                         selection.Items, segments);
        return;
    }

    if (auto command = MeshEdit.ApplyVerb(*Interaction.Sink, selection, MeshEditVerb::BridgeEdges,
                                          { .BridgeSegments = segments }))
    {
        Commands->Execute(std::move(command));
        Selection.ClearMeshElementSelections();
    }
}

void EditorWorkspace::CommitPendingBridge()
{
    if (Commands != nullptr)
        BridgeEdit.Commit(*Commands, Selection);
}

void EditorWorkspace::CancelPendingBridge()
{
    if (Commands != nullptr)
        BridgeEdit.Cancel(*Commands);
}

void EditorWorkspace::BeginInsetOnSelectedFaces(float distance)
{
    if (Commands == nullptr || Interaction.Sink == nullptr)
        return;
    ElementEdit.BeginInset(*Interaction.Sink, *Commands, Selection.GetSelection(), distance);
}

void EditorWorkspace::BeginBevelOnSelectedEdges(float width, int segments)
{
    if (Commands == nullptr || Interaction.Sink == nullptr)
        return;
    ElementEdit.BeginBevel(*Interaction.Sink, *Commands, Selection.GetSelection(), width, segments);
}

void EditorWorkspace::CommitPendingElementEdit()
{
    if (Commands != nullptr)
        ElementEdit.Commit(*Commands);
}

void EditorWorkspace::CancelPendingElementEdit()
{
    if (Commands != nullptr)
        ElementEdit.Cancel(*Commands);
}
