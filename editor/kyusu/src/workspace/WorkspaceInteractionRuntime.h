#pragma once

#include "editmodes/EditSessionHost.h"
#include "interaction/InteractionHost.h"
#include "overlay/EditorOverlayState.h"
#include "render/PreviewBuffer.h"
#include "tools/ToolContext.h"
#include "viewport/MarqueeState.h"

#include <math/Vec.h>

#include <functional>
#include <memory>

class BrushManipulationSink;
class CommandStack;
class EditorAffordanceService;
class ManipulatorSession;
class MeshEditService;
class PickingService;
class SelectionContext;
class SelectionService;
class ToolRegistry;
class ViewportLayout;
class ViewportToolDispatcher;
class WorldDocument;
struct PivotState;

// Workspace state the editing stack binds but does not own. All of it outlives
// any one document, which is why the runtime holds it by reference and rebuilds
// against it rather than capturing a document.
struct WorkspaceInteractionInputs
{
    WorldDocument& World;
    SelectionContext& LevelSelection;
    SelectionService& Selection;
    PickingService& Picking;
    MeshEditService& MeshEdit;
    ViewportLayout& Layout;
    PivotState& Pivot;
    EditorAffordanceService& Affordances;
    ToolAuthoringSettings Settings;
    // Raised when a manipulator drag commits a duplicate, carrying the offset it
    // moved by, so the workspace can record it as the repeatable action.
    std::function<void(Vec3d)> OnDuplicateCommitted;
};

// The document-bound editing stack: the manipulation sink every scene mutation
// during manipulation goes through, the tool context and registry over it, the
// viewport dispatcher, and the manipulator session.
//
// Also owns the transient state that dies with the edited document (the live
// interaction, its preview geometry, the marquee, and the viewport overlay), so
// a rebuild cannot leave any of it pointing at the outgoing document.
//
// Rebuild is the only way the stack is created. Document open and focus change
// both go through it, which is what keeps them one path instead of two.
class WorkspaceInteractionRuntime
{
public:
    // Out of line: the owned stack members are forward-declared here, so their
    // destructors are only complete in the implementation.
    WorkspaceInteractionRuntime();
    ~WorkspaceInteractionRuntime();

    // Drops the previous stack and builds a new one against the focused
    // document. Safe to call with no stack built yet.
    void Rebuild(const WorkspaceInteractionInputs& inputs, CommandStack& commands);

    // Drops the transient state without rebuilding the stack.
    void ClearTransient();

    // Cancels whatever the active tool is mid-gesture. No-op before the first
    // Rebuild.
    void CancelActiveTool();

    InteractionHost Interactions;
    PreviewBuffer Preview;
    MarqueeState Marquee;
    EditorOverlayState Overlay;
    EditSessionHost Sessions;

    std::unique_ptr<BrushManipulationSink> Sink;
    std::unique_ptr<ToolContext> Context;
    std::unique_ptr<ToolRegistry> Tools;
    std::unique_ptr<ViewportToolDispatcher> Dispatcher;
    // Non-owning; Sessions owns the session. Held so the overlay renderer can
    // ask it for manipulator visuals.
    ManipulatorSession* Manipulators = nullptr;
};
