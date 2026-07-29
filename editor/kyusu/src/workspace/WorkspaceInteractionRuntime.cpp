#include "WorkspaceInteractionRuntime.h"

#include "BrushManipulationSink.h"
#include "authoring/EditorComponentAdapter.h"
#include "document/EditorDocument.h"
#include "document/EditorEntityRecipe.h"
#include "document/EditorScene.h"
#include "document/WorldDocument.h"
#include "document/tools/BrushTool.h"
#include "document/tools/EdgeCutTool.h"
#include "document/tools/FaceCarveTool.h"
#include "document/tools/SelectTool.h"
#include "editmodes/ManipulatorSession.h"
#include "editmodes/PivotState.h"
#include "input/ViewportToolDispatcher.h"
#include "meshedit/MeshEditService.h"
#include "selection/SelectionContext.h"
#include "selection/SelectionService.h"
#include "tools/ToolRegistry.h"
#include "viewport/Picking.h"
#include "viewport/ViewportLayout.h"

#include <utility>

WorkspaceInteractionRuntime::WorkspaceInteractionRuntime() = default;
WorkspaceInteractionRuntime::~WorkspaceInteractionRuntime() = default;

void WorkspaceInteractionRuntime::Rebuild(const WorkspaceInteractionInputs& inputs,
                                          CommandStack& commands)
{
    EditorDocument& document = inputs.World.FocusDocument();

    // Only the focus document is selectable (context zones are unpickable by
    // construction); the guard turns any stray cross-registry ref into a loud
    // debug failure instead of a silent stale selection.
    inputs.LevelSelection.SetExpectedRegistry(document.GetRegistry().Id);

    // All scene mutation during manipulation goes through this one sink; the
    // session, manipulators, and the edge-cut tool stay scene-agnostic. Built
    // first so the tool context can hold it.
    WorldDocument& world = inputs.World;
    Sink = std::make_unique<BrushManipulationSink>(
        document.GetScene(), document, commands, inputs.Selection,
        [&world](std::vector<EntitySnapshot>& snapshots)
        { RemapWorldConnectionDuplicateSnapshots(world, snapshots); });
    if (inputs.OnDuplicateCommitted)
        Sink->SetDuplicateObserver(inputs.OnDuplicateCommitted);

    Context = std::make_unique<ToolContext>(
        commands,
        inputs.Selection,
        inputs.Picking,
        document.GetScene(),
        document,
        inputs.MeshEdit,
        *Sink,
        ToolInteractionState{ Interactions, Preview, Marquee, Overlay },
        inputs.Settings);

    Tools = std::make_unique<ToolRegistry>(*Context);
    Tools->Register(std::make_unique<SelectTool>());
    Tools->Register(std::make_unique<BrushTool>());
    Tools->Register(std::make_unique<EdgeCutTool>());
    Tools->Register(std::make_unique<FaceCarveTool>());
    Tools->Activate("select");

    // Lets a tool hand off to another (the edge cut switches to Select after a cut).
    Context->ActivateTool = [this](std::string_view id) { Tools->Activate(id); };

    Dispatcher = std::make_unique<ViewportToolDispatcher>(
        inputs.Layout,
        *Context,
        Interactions,
        Sessions,
        *Tools);

    // The session reads selection and element mode live on each pointer-down, so
    // it never needs rebuilding when the selection or mode changes. It consumes a
    // click only when a manipulator is hit; otherwise the select tool picks.
    auto session = std::make_unique<ManipulatorSession>(
        inputs.Selection, inputs.MeshEdit, *Sink, inputs.Settings.Grid, inputs.Pivot,
        inputs.Affordances);
    Manipulators = session.get();
    Sessions.SetSession(std::move(session));

    // Resize quietly yields to Move while the selection has nothing resizable.
    SelectionService& selection = inputs.Selection;
    EditorAffordanceService& affordances = inputs.Affordances;
    Manipulators->SetResizableQuery(
        [&world, &selection, &affordances]
        {
            const EditorScene& scene = world.FocusDocument().GetScene();
            for (const SelectableRef& ref : selection.GetSelection())
                if (ref.IsEntity() && scene.TryGetBrushMesh(ref.Entity) != nullptr)
                    return true;
            return affordances.HasEditTargets();
        });
    Manipulators->SetScaleAllowedQuery(
        [&affordances] { return affordances.AllowsScaleForSelection(); });
}

void WorkspaceInteractionRuntime::ClearTransient()
{
    Marquee = {};
    Overlay.Labels.clear();
    Overlay.PointHandles.clear();
    Overlay.Readout.Clear();
    Overlay.Hover = {};
    Overlay.HoverBody = {};
    Preview.Clear();
}

void WorkspaceInteractionRuntime::CancelActiveTool()
{
    if (Tools != nullptr)
        Tools->Cancel();
}
