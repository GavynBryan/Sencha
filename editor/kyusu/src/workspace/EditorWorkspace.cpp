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

EditorWorkspace::EditorWorkspace(LoggingProvider& logging, CommandStack& commands)
    : World(logging)
    , Selection(LevelSelection)
    , MeshEdit(logging)
    , Commands(commands)
    , Actions(World, Selection, commands, MeshEdit, BridgeEdit,
              [this] { return static_cast<IMeshEditTarget*>(Interaction.Sink.get()); },
              MakeDuplicateRemap())
{
    World.BindViewSettings(&WorldView);
    PendingPanelEdits = {
        PendingEditHooks{
            [this] { return BridgeEdit.HasPending(); },
            [this] { BridgeEdit.Commit(Commands, Selection); },
            [this] { BridgeEdit.Cancel(Commands); },
        },
        PendingEditHooks{
            [this] { return ElementEdit.HasPendingInset() || ElementEdit.HasPendingBevel(); },
            [this] { ElementEdit.Commit(Commands); },
            [this] { ElementEdit.Cancel(Commands); },
        },
    };
    Build();
}

bool EditorWorkspace::HasPendingPanelEdit() const
{
    for (const PendingEditHooks& pending : PendingPanelEdits)
        if (pending.HasPending())
            return true;
    return false;
}

std::function<void(std::vector<EntitySnapshot>&)> EditorWorkspace::MakeDuplicateRemap()
{
    // Copies of world docks and links must not share the original's minted ids.
    // Every duplicate route (menu, gizmo drag, repeat) goes through this, so a
    // route cannot quietly skip the remap and produce colliding identity.
    return [this](std::vector<EntitySnapshot>& snapshots)
    { RemapWorldConnectionDuplicateSnapshots(World, snapshots); };
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
            ToolAuthoringSettings{ Grid, ActiveMaterial },
            MakeDuplicateRemap(),
            // A committed duplicate (the gizmo Shift-drag) becomes the repeatable
            // action: Ctrl+R re-duplicates the current selection at the same offset.
            [this](Vec3d offset) { Actions.RecordRepeatableDuplicate(offset); },
        },
        Commands);
}

void EditorWorkspace::CancelDocumentTransactions()
{
    Interaction.CancelActiveTool();
    // Every preview stages state in a document: the bridge owns an entity in the
    // scene it was begun in, the element edit holds the pre-edit meshes. Cancel
    // them (rather than dropping the stack under them) so the staged state is
    // reverted while the document that holds it is still alive.
    for (const PendingEditHooks& pending : PendingPanelEdits)
        pending.Cancel();
    Commands.Clear();
}

void EditorWorkspace::ResolvePendingEdits()
{
    // Commit, not cancel: a live preview is what the user is looking at, and
    // persisting the document behind their back with that geometry missing
    // would be the surprising outcome. Each resolution is its own undo step, so
    // an unwanted commit is one Ctrl+Z away. Tools whose preview is a hover
    // artifact rather than placed work revert instead (see ITool::CommitPending).
    Interaction.CommitActiveTool();
    for (const PendingEditHooks& pending : PendingPanelEdits)
        pending.Commit();
}

// Points the selection at the focused document: identity comes from that
// document's index, and a rebuild of its projection retargets the live
// selection instead of leaving it holding recreated entities' dead handles.
void EditorWorkspace::BindSelectionToActiveDocument()
{
    EditorDocument& document = ActiveDocument();
    Selection.BindDocument(&document.GetScene().GetRegistry().Components);
    document.SetProjectionObserver([this] { Selection.RetargetToDocument(); });
}

void EditorWorkspace::ResetInteractionState()
{
    CancelDocumentTransactions();
    Selection.ClearSelection();

    // The selection addresses one document, so identity stamping and handle
    // resolution follow the focus. Rebinding before the tools rebuild means a
    // ref picked in the new document is identified from its first frame.
    BindSelectionToActiveDocument();

    // Transient view/interaction state that may reference the outgoing document.
    Interaction.ClearTransient();
    Pivot = {};

    BuildInteractionState();
}

void EditorWorkspace::Build()
{
    Affordances = std::make_unique<EditorAffordanceService>(
        World, Selection, Commands, Grid);
    Affordances->Registry().Register(MakeWorldDockEditorAdapter());
    CreationRecipes.Register("world_dock", std::make_unique<WorldDockRecipe>());
    Picking.SetEntityProxyProvider(
        [this](const Ray3d& ray, const EditorScene& scene)
        { return Affordances->Pick(ray, scene); });

    BuildInteractionState();

    // Anything that leaves a different document under the editing stack (zone
    // focus, world scene focus, new, open, close to legacy) lands here, so the
    // stack rebinds from one place instead of each caller arranging it.
    World.OnEditedDocumentChanged = [this] { ResetInteractionState(); };
    BindSelectionToActiveDocument();

    // The documents a preview staged state into are about to be destroyed.
    // Unwind now, while they are still alive; the rebuild follows from the
    // focus change or the file action once the new documents exist.
    World.OnWillReplaceDocuments = [this] { CancelDocumentTransactions(); };

    // An unload destroys a zone document that queued commands may still
    // reference (the cross-zone move holds two documents). Clearing the stack
    // would drop an open pending edit's cancel without running it, stranding the
    // preview in the focus document (which the unload leaves alive), so cancel
    // the live transactions first. Focus is unchanged, so tools and selection stay.
    World.OnZoneUnloaded = [this](ZoneId) { CancelDocumentTransactions(); };

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

    const EditorScene& scene = ActiveDocument().GetScene();
    const MeshElementKind kind = MeshEdit.GetElementKind();
    const RegistryId registry = scene.GetRegistry().Id;

    std::vector<SelectableRef> gathered;
    if (kind == MeshElementKind::Object)
    {
        for (EntityId entity : scene.GetAllEntities())
            if (scene.IsEntityEffectivelyVisible(entity) && !scene.IsEntityEffectivelyLocked(entity))
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
            const Transform3f* transform = scene.TryGetWorldTransform(entity);
            if (mesh == nullptr || transform == nullptr)
                continue;
            std::vector<SelectableRef> refs = MeshElements::AllRefs(*mesh, *transform, registry, entity, kind);
            gathered.insert(gathered.end(), refs.begin(), refs.end());
        }
    }

    if (gathered.empty())
        return;

    SelectionSnapshot snapshot = SelectionFold::Apply({}, gathered, SelectionFold::Op::Replace);
    Commands.Execute(std::make_unique<SelectCommand>(Selection, std::move(snapshot)));
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

    bool hasElementRefs = false;
    for (const SelectableRef& ref : Selection.GetSelection())
        if (ref.IsMeshElement())
        {
            hasElementRefs = true;
            break;
        }

    switch (NextEscapeAction(EscapeContext{
                .HasPendingEdit = HasPendingPanelEdit(),
                .GridOriginEditing = Interaction.Manipulators->IsEditingGridOrigin(),
                .PivotEditing = Pivot.Editing,
                .HasElementRefs = hasElementRefs,
                .ElementKind = MeshEdit.GetElementKind(),
                .HasSelection = !Selection.GetSelection().empty(),
            }))
    {
    case EscapeAction::CancelPendingEdit:
        for (const PendingEditHooks& pending : PendingPanelEdits)
            pending.Cancel();
        break;
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
        Commands.Execute(std::make_unique<SelectCommand>(Selection, std::move(entitiesOnly)));
        break;
    }
    case EscapeAction::DropToObjectMode:
        MeshEdit.SetElementKind(MeshElementKind::Object);
        break;
    case EscapeAction::ClearSelection:
        Commands.Execute(std::make_unique<SelectCommand>(Selection, SelectionSnapshot{}));
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
        const Transform3f* transform = scene.TryGetWorldTransform(ref.Entity);
        if (mesh == nullptr || transform == nullptr)
            continue;
        const Aabb3d entityBounds = BrushWorldBounds(*mesh, *transform);
        if (entityBounds.IsValid())
            bounds.ExpandToInclude(entityBounds);
    }

    // Appended, not assigned: clear() above keeps the vector's capacity, and a
    // move-assign here would throw that away every frame.
    if (bounds.IsValid())
        AppendSelectionDimensionLabels(bounds, EditorTheme::DimensionLabel,
                                       Interaction.Overlay.Labels);

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
    if (!Pivot.Override.has_value())
        return;

    const SelectableRef primary = Selection.GetPrimarySelection();
    if (!primary.IsEntity())
        return;

    if (auto command = MakeSetBrushOriginCommand(ActiveDocument().GetScene(), primary.Entity, *Pivot.Override))
    {
        Commands.Execute(std::move(command));
        Pivot.Override.reset(); // the origin is now the pivot; drop the transient
    }
}

void EditorWorkspace::SetSelectedBrushOrigin(OriginAnchor anchor)
{

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
        Commands.Execute(std::move(command));
        Pivot.Override.reset(); // the pivot tracked the old origin; drop the transient
    }
}

void EditorWorkspace::ApplyActiveMaterialToSelectedFaces()
{
    if (!ActiveMaterial.Active.IsValid() || Interaction.Sink == nullptr)
        return;
    ApplyMaterialToSelectedFaces(*Interaction.Sink, Selection, Commands, ActiveMaterial.Active);
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
    if (!UvClipboard.has_value() || Interaction.Sink == nullptr)
        return;
    PasteFaceProjection(*Interaction.Sink, Selection, Commands, *UvClipboard);
}

void EditorWorkspace::DeleteSelection()
{

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
            Commands.Execute(std::move(command));
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

    Commands.Execute(MakeDeleteEntitiesCommand(entities, ActiveDocument().GetScene(), ActiveDocument(), Selection));
}

void EditorWorkspace::DissolveSelectedEdges()
{
    if (Interaction.Sink == nullptr)
        return;
    if (auto command = MeshEdit.ApplyVerb(*Interaction.Sink, Selection.GetSnapshot(), MeshEditVerb::DissolveEdge, {}))
    {
        Commands.Execute(std::move(command));
        Selection.ClearMeshElementSelections();
    }
}

void EditorWorkspace::CommitPendingBridge()
{
    BridgeEdit.Commit(Commands, Selection);
}

void EditorWorkspace::CancelPendingBridge()
{
    BridgeEdit.Cancel(Commands);
}

void EditorWorkspace::BeginInsetOnSelectedFaces(float distance)
{
    if (Interaction.Sink == nullptr)
        return;
    ElementEdit.BeginInset(*Interaction.Sink, Commands, Selection.GetSelection(), distance);
}

void EditorWorkspace::BeginBevelOnSelectedEdges(float width, int segments)
{
    if (Interaction.Sink == nullptr)
        return;
    ElementEdit.BeginBevel(*Interaction.Sink, Commands, Selection.GetSelection(), width, segments);
}

void EditorWorkspace::CommitPendingElementEdit()
{
    ElementEdit.Commit(Commands);
}

void EditorWorkspace::CancelPendingElementEdit()
{
    ElementEdit.Cancel(Commands);
}
