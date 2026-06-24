#include "EditorWorkspace.h"

#include "../EditorTheme.h"
#include "../brush/BrushBounds.h"
#include "../document/EditorScene.h"
#include "../document/commands/DeleteEntityCommand.h"
#include "../document/commands/SetBrushOriginCommand.h"
#include "../document/tools/CameraTool.h"
#include "../document/tools/BrushTool.h"
#include "../document/tools/EdgeCutTool.h"
#include "../document/tools/SelectTool.h"
#include "../overlay/SelectionLabels.h"

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
    Tools->Register(std::make_unique<CameraTool>());
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

    // The transient pivot is per-selection: any selection change resets it to the
    // computed center. (The Editing toggle is a mode and persists.)
    PivotObserver = Selection.Subscribe([this](const SelectionSnapshot&) { Pivot.Override.reset(); });
}

void EditorWorkspace::UpdateOverlay()
{
    Overlay.Labels.clear();

    // Union the world bounds of every selected brush, so the dimension labels
    // describe the selection as one box (Hammer shows the selection's extents).
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
