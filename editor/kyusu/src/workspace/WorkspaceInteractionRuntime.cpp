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

    // Tear the outgoing stack down before building the new one, innermost
    // dependent first. Building over it would free the sink while the context,
    // registry, and session still referenced it.
    if (Tools != nullptr)
        Tools->Cancel();
    Dispatcher.reset();
    Context.reset();
    Sink.reset();

    // All scene mutation during manipulation goes through this one sink; the
    // session, manipulators, and the edge-cut tool stay scene-agnostic. Built
    // first so the tool context can hold it.
    WorldDocument& world = inputs.World;
    Sink = std::make_unique<BrushManipulationSink>(
        document.GetScene(), document, commands, inputs.Selection, inputs.DuplicateRemap);
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

    // The tools and the manipulator session are editor-lifetime: they hold
    // authoring settings and gizmo preferences that belong to the user, not to a
    // document. Built once, then rebound, so a focus change cannot silently
    // reset the active tool, the gizmo mode, or a tool's own settings.
    if (Tools == nullptr)
    {
        Tools = std::make_unique<ToolRegistry>(*Context);
        Tools->Register(std::make_unique<SelectTool>());
        Tools->Register(std::make_unique<BrushTool>());
        Tools->Register(std::make_unique<EdgeCutTool>());
        Tools->Register(std::make_unique<FaceCarveTool>());
        Tools->Activate("select");
    }
    else
    {
        // Rebind only: re-running OnActivate here would push the entered tool's
        // setup (the brush tool forces Object mode) onto a document swap.
        Tools->Rebind(*Context);
    }

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
    if (Manipulators == nullptr)
    {
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
    else
    {
        Manipulators->SetSink(*Sink);
    }
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

void WorkspaceInteractionRuntime::CommitActiveTool()
{
    if (Tools == nullptr || Context == nullptr)
        return;
    if (ITool* active = Tools->GetActiveTool())
        active->CommitPending(*Context);
}
