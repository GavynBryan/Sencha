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

    // Duplicates the entity-kind selection in place as one undoable step and
    // selects the copies. With asInstance, brush copies SHARE the source's mesh
    // (editing any instance edits them all; baking them shares one asset).
    void DuplicateSelection(bool asInstance);

    // Breaks every selected instanced brush out of its instance group (each gets
    // its own copy of the live shared mesh), as one undoable step.
    void MakeSelectedBrushesUnique();

    // Joins the selected brushes into the primary one (faces rebased, texture
    // placement preserved, sources destroyed), as one undoable step.
    void MergeSelectedBrushes();

    // Splits the selected faces (one brush) into a new brush entity, as one
    // undoable step; the source keeps at least one face.
    void SeparateSelectedFaces();

    // Select-all for the current element kind, as one undoable step. Object mode:
    // every visible, unlocked entity. Element modes: every element of that kind on
    // the brushes in the current selection (no-op when none are selected).
    void SelectAll();

    // One Escape press climbs one level of editing context (see EscapePolicy.h):
    // pivot edit, then element selection, then element mode, then the selection.
    void EscapeStep();

    // Keeps the fixed ortho views pointed down the grid frame's axes (Top =
    // frame normal, Front = frame depth, ...), so a moved/rotated working grid
    // stays axis-aligned in every ortho view. Cheap and idempotent; called once
    // per frame.
    void SyncOrthoViewsToGridFrame();

    // Grid frame verbs. The frame is view state (like the camera), not document
    // state, so none of these are undo steps.
    // Origin to the selection: a single selected vertex wins (exact), otherwise
    // the center of the selection's world bounds. No-op when neither resolves.
    void SetGridOriginToSelection();
    // Aligns the frame to the primary selected face: origin at its centroid, U
    // along its longest edge, V completing the basis in the face plane.
    void AlignGridToSelectedFace();
    void RotateGridInPlane(float degrees);
    void ResetGrid();

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
    // Drops the element kind back to Object when the selection turns into plain
    // entities only (nothing brush-editable), so entity work never dead-ends in a
    // mesh-element mode.
    std::shared_ptr<SelectionService::ObserverFn> ModeObserver;
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
