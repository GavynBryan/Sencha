#pragma once

#include "PendingBridgeEdit.h"
#include "SelectionActions.h"
#include "PendingElementEdit.h"
#include "WorkspaceInteractionRuntime.h"

#include "authoring/EditorComponentAdapter.h"
#include "commands/CommandStack.h"
#include "document/EditorEntityRecipe.h"
#include "editmodes/PivotState.h"
#include "meshedit/ActiveMaterialState.h"
#include "meshedit/FaceMaterialEdits.h"
#include "meshedit/MeshEditService.h"
#include "overlay/EditorOverlayState.h"
#include "selection/SelectionContext.h"
#include "selection/SelectionService.h"
#include "viewport/GridSettings.h"
#include "viewport/WorldViewSettings.h"
#include "viewport/Picking.h"
#include "viewport/ViewportLayout.h"

#include "document/WorldDocument.h"

#include <functional>
#include <memory>
#include <array>
#include <optional>

class LoggingProvider;

class EditorWorkspace
{
public:
    EditorWorkspace(LoggingProvider& logging, CommandStack& commands);

    // The document all editing binds: the focus zone's document in world mode,
    // the single anonymous document in legacy mode. Long-lived objects resolve
    // through here at each use instead of holding EditorDocument&/EditorScene&
    // members, so a focus change cannot leave them stale.
    [[nodiscard]] EditorDocument& ActiveDocument() { return World.FocusDocument(); }
    [[nodiscard]] const EditorDocument& ActiveDocument() const { return World.FocusDocument(); }

    // Terminates every transaction staging state in the active document (the
    // active tool's gesture, the pending bridge, the pending element edit) and
    // drops the undo stack. Safe to run immediately before the document is
    // destroyed: it touches only state that is still alive at the call.
    void CancelDocumentTransactions();

    // Settles every open preview into the document as ordinary undo steps, so
    // that nothing reaches a file or a cook in a half-staged state. Save, Save
    // As, and cook run it first. This is the one place the resolve-by-commit
    // policy lives.
    void ResolvePendingEdits();

    // Tears down and rebuilds everything that binds the active document (tool
    // context, manipulation sink, session, transient interaction state) and
    // clears the selection and the undo stack. Document open and focus change
    // both swap the edited document, so both reset through this one path.
    void ResetInteractionState();

    // Deletes the entity-kind selection as one undoable step (no-op if empty).
    void DeleteSelection();
    // Backspace verb in edge mode: merge the faces across each selected edge.
    void DissolveSelectedEdges();
    [[nodiscard]] bool HasPendingBridge() const { return BridgeEdit.HasPending(); }
    // Regenerates the pending preview with a new segment count.
    void SetPendingBridgeSegments(int segments) { BridgeEdit.SetSegments(segments); }
    void CommitPendingBridge();
    void CancelPendingBridge();

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

    // Anchor choices for re-origining the primary selected brush without moving
    // its geometry: the first selected vertex, its world-bounds center, or the
    // world-bounds minimum corner.
    enum class OriginAnchor : std::uint8_t
    {
        SelectedVertex,
        BoundsCenter,
        BoundsMinCorner,
    };
    // Re-origins the primary selected brush to the anchor (no-op when the anchor
    // does not resolve), then clears the transient pivot. One undoable step.
    void SetSelectedBrushOrigin(OriginAnchor anchor);

    // Applies the active material to every selected face, one undoable step per
    // brush. No-op when no material is active or nothing face-like is selected.
    void ApplyActiveMaterialToSelectedFaces();

    // Stash the primary selected face's material + projection into UvClipboard
    // (kept when nothing face-like is selected, so a failed copy never clobbers
    // a good clipboard).
    void CopySelectedFaceProjection();
    // Apply the clipboard's material + projection to every selected face.
    void PasteFaceProjectionToSelection();

    WorldDocument World;
    WorldViewSettings WorldView;
    ViewportLayout Layout = ViewportLayout::MakeDefault();
    SelectionContext LevelSelection;
    SelectionService Selection;
    PickingService Picking;
    std::unique_ptr<EditorAffordanceService> Affordances;
    EditorEntityRecipeRegistry CreationRecipes;
    MeshEditService MeshEdit;
    // Editor-wide texturing state: the browser and viewport sampling write it,
    // the apply verbs read it.
    ActiveMaterialState ActiveMaterial;
    // Face projection clipboard. Lives here (not in a panel) so the copy/paste
    // shortcuts work with every panel closed.
    std::optional<FaceMaterialClipboard> UvClipboard;
    GridSettings Grid;
    PivotState Pivot;
    // The document-bound editing stack and the transient state that dies with
    // the edited document.
    WorkspaceInteractionRuntime Interaction;
    // Keeps the deselect observer alive (SelectionService holds it weakly); clears
    // the transient pivot override whenever the selection changes.
    std::shared_ptr<SelectionService::ObserverFn> PivotObserver;
    // Drops the element kind back to Object when the selection turns into plain
    // entities only (nothing brush-editable), so entity work never dead-ends in a
    // mesh-element mode.
    std::shared_ptr<SelectionService::ObserverFn> ModeObserver;
    // Entity/mesh selection and Zone selection are mutually exclusive while
    // still sharing one visible selection across panels and viewport affordances.
    std::shared_ptr<SelectionService::ObserverFn> ZoneSelectionObserver;
    // Owned by EditorServices, which declares it ahead of the workspace and so
    // outlives it. Workspace-level edits (DeleteSelection) route through the
    // same undo history every other surface writes to.
    CommandStack& Commands;

    // Cross-brush bridge preview. Owns the staged entity, the paths it
    // regenerates from, and the pending-edit scope that goes with them.
    PendingBridgeEdit BridgeEdit;

    // Panel-driven face inset / edge bevel preview. Owns the captured meshes
    // and the pending-edit scope that goes with them.
    PendingElementEdit ElementEdit;

    // Verbs over the selection as a whole (duplicate, merge, separate, bake).
    // Declared after the members it binds so construction order holds.
    SelectionActions Actions;

private:
    // One entry per panel-driven pending-edit mechanism. The lifecycle verbs
    // (Escape, document teardown, save resolution) iterate this rather than
    // naming each mechanism, so a new mechanism is registered once here instead
    // of being remembered at every site that has to resolve a staged preview.
    //
    // Order does not matter: the command stack holds a single pending-edit
    // scope, so at most one entry ever has something staged.
    struct PendingEditHooks
    {
        std::function<bool()> HasPending;
        std::function<void()> Commit;
        std::function<void()> Cancel;
    };
    std::array<PendingEditHooks, 2> PendingPanelEdits;

    // True while any panel-driven preview is staged.
    [[nodiscard]] bool HasPendingPanelEdit() const;

public:
    void BeginInsetOnSelectedFaces(float distance);
    [[nodiscard]] bool HasPendingInset() const { return ElementEdit.HasPendingInset(); }
    void SetPendingInsetDistance(float distance) { ElementEdit.SetInsetDistance(distance); }
    void BeginBevelOnSelectedEdges(float width, int segments);
    [[nodiscard]] bool HasPendingBevel() const { return ElementEdit.HasPendingBevel(); }
    void SetPendingBevelParams(float width, int segments) { ElementEdit.SetBevelParams(width, segments); }
    void CommitPendingElementEdit();
    void CancelPendingElementEdit();

private:
    // The identity remap every duplicate route shares. See the definition.
    [[nodiscard]] std::function<void(std::vector<EntitySnapshot>&)> MakeDuplicateRemap();

private:
    // Composes the authoring subsystems and subscribes the document and
    // selection observers. Runs once, from the constructor.
    void Build();

    // Builds the document-bound editing stack (sink, tool context, dispatcher)
    // against the active document and rebinds the editor-lifetime tools and
    // manipulator session to it. Bring-up and ResetInteractionState share it.
    void BuildInteractionState();

    // Re-expresses the current selection in the new element kind (face -> its
    // edges, edge -> endpoints, and so on) so switching modes carries the
    // selection along. No-op when nothing needs converting, so it is safe when
    // the kind change fires from inside a selection notification.
    void ConvertSelectionToKind(MeshElementKind next);
};
