#pragma once

#include "BrushManipulationSink.h"

#include "../commands/CommandStack.h"
#include "../editmodes/EditSessionHost.h"
#include "../editmodes/ManipulatorSession.h"
#include "../input/ViewportToolDispatcher.h"
#include "../interaction/InteractionHost.h"
#include "../meshedit/MeshEditService.h"
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

    EditorDocument Document;
    ViewportLayout Layout = ViewportLayout::MakeFourWay();
    SelectionContext LevelSelection;
    SelectionService Selection;
    PickingService Picking;
    MeshEditService MeshEdit;
    GridSettings Grid;
    BrushCreationSettings BrushCreate;
    MarqueeState Marquee;
    InteractionHost Interactions;
    PreviewBuffer Preview;
    EditSessionHost Sessions;
    std::unique_ptr<BrushManipulationSink> Sink;
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
