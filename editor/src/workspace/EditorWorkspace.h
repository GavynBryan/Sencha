#pragma once

#include "BrushManipulationSink.h"

#include "../commands/CommandStack.h"
#include "../editmodes/EditSessionHost.h"
#include "../editmodes/ManipulatorSession.h"
#include "../input/ViewportToolDispatcher.h"
#include "../editmodes/PivotState.h"
#include "../interaction/InteractionHost.h"
#include "../meshedit/MeshEditService.h"
#include "../overlay/EditorOverlayState.h"
#include "../render/PreviewBuffer.h"
#include "../selection/SelectionContext.h"
#include "../selection/SelectionService.h"
#include "../tools/ToolContext.h"
#include "../tools/ToolRegistry.h"
#include "../viewport/GridSettings.h"
#include "../viewport/MarqueeState.h"
#include "../viewport/Picking.h"
#include "../viewport/ViewportLayout.h"

#include "../document/BrushCreationSettings.h"
#include "../document/EdgeCutSettings.h"
#include "../document/EditorDocument.h"

#include <functional>
#include <memory>

class LoggingProvider;

class EditorWorkspace
{
public:
    explicit EditorWorkspace(LoggingProvider& logging);

    void Init(CommandStack& commands);

    // Deletes the entity-kind selection as one undoable step (no-op if empty).
    void DeleteSelection();

    // Rebuilds the per-frame overlay labels (selected-brush dimensions) from the
    // current selection. Cheap; called once per frame before the UI draws. Leaves
    // the drag readout alone (the active interaction owns it).
    void UpdateOverlay();

    // Re-origins the primary selected brush to the moved pivot (no-op if the pivot
    // has not been moved), then clears the transient pivot. One undoable step.
    void SetSelectedBrushOriginToPivot();

    EditorDocument Document;
    ViewportLayout Layout = ViewportLayout::MakeFourWay();
    SelectionContext LevelSelection;
    SelectionService Selection;
    PickingService Picking;
    MeshEditService MeshEdit;
    GridSettings Grid;
    BrushCreationSettings BrushCreate;
    EdgeCutSettings EdgeCut;
    MarqueeState Marquee;
    EditorOverlayState Overlay;
    PivotState Pivot;
    InteractionHost Interactions;
    PreviewBuffer Preview;
    EditSessionHost Sessions;
    std::unique_ptr<BrushManipulationSink> Sink;
    // Keeps the deselect observer alive (SelectionService holds it weakly); clears
    // the transient pivot override whenever the selection changes.
    std::shared_ptr<SelectionService::ObserverFn> PivotObserver;
    // Non-owning; the EditSessionHost owns the session. Held so the overlay
    // renderer can ask it for manipulator visuals.
    ManipulatorSession* Manipulators = nullptr;
    std::unique_ptr<ToolContext> ActiveToolContext;
    std::unique_ptr<ToolRegistry> Tools;
    std::unique_ptr<ViewportToolDispatcher> Dispatcher;
    // Non-owning; the command stack passed to Init (owned by EditorServices), held
    // so workspace-level edits (DeleteSelection) route through the same undo history.
    CommandStack* Commands = nullptr;
};
