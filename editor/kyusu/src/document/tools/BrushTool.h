#pragma once

#include "tools/ITool.h"

#include "selection/SelectableRef.h"

#include <ecs/EntityId.h>
#include <math/Quat.h>
#include <math/Vec.h>

#include <vector>

class BrushTool : public ITool
{
public:
    // Brush creation lifecycle. Invariants: State == Pending exactly while
    // SetPending's scene entity (PendingEntity) is alive and
    // PendingPreviousSelection holds the selection to restore on cancel.
    // Transitions happen only in SetPending (Idle -> Pending) and
    // CommitPending / CancelPending (Pending -> Idle).
    // Dragging is deliberately not a state here: the live
    // BrushCreateDragInteraction owned by the InteractionHost is that phase,
    // and BeginDrag commits any pending brush first, so while a create-drag
    // runs the tool is Idle.
    enum class BrushToolState : std::uint8_t { Idle, Pending };

    // A drag-authored primitive awaiting commit. Placement is captured once at
    // drag release; from then on the live scene entity owns the transform (so
    // gizmo edits to the pending brush stick) and the generator settings
    // reshape only the local mesh.
    struct PendingBrush
    {
        Vec3d Center{};
        Vec3d HalfExtents{ 0.5f, 0.5f, 0.5f };
        Quatf Rotation = Quatf::Identity();
        int DepthAxis = 1;
        // Side of the drag's rest plane the depth extent grows toward,
        // measured along the brush's local depth axis.
        float DepthSign = 1.0f;
        // The drag's own depth half-extent (zero for a Plane drag).
        float DragDepthHalf = 0.0f;
        // Minimum depth for a solid regenerated from a zero-depth drag;
        // captured from the grid at SetPending, never re-read afterwards.
        float DepthFloorHalf = 0.0f;
        // Depth half the most recent rebuild used. A primitive-class change
        // shifts the entity along its depth axis by the difference so the
        // rest side stays fixed.
        float LastBuiltDepthHalf = 0.0f;
        // Local AABB of the mesh the most recent rebuild generated. When the
        // live mesh's bounds differ, an external edit reshaped the pending
        // brush (bounds handles, face drags); RefreshPending adopts the live
        // bounds into the generation parameters instead of wiping the edit.
        Vec3d LastBuiltBoundsMin{};
        Vec3d LastBuiltBoundsMax{};
    };

    std::string_view GetId() const override;
    std::string_view GetDisplayName() const override;
    std::string_view GetIcon() const override;
    void OnActivate(ToolContext& ctx) override;
    void OnDeactivate(ToolContext& ctx) override;
    void OnCancel(ToolContext& ctx) override;
    InputConsumed OnClick(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override;
    InputConsumed OnKeyDown(ToolContext& ctx, const KeyDownEvent& event) override;
    std::unique_ptr<IInteraction> BeginDrag(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pressPointer) override;

    [[nodiscard]] bool HasPending() const { return State == BrushToolState::Pending; }

    // Called by the create-drag interaction on release; takes ownership of the
    // preview until commit or cancel.
    void SetPending(ToolContext& ctx, const PendingBrush& pending);

    // Rebuilds the pending preview after a BrushCreationSettings edit.
    void RefreshPending(ToolContext& ctx);

    // Commits the pending brush as one undoable create and drops the preview.
    void CommitPending(ToolContext& ctx);
    void CancelPending(ToolContext& ctx);

private:
    BrushToolState State = BrushToolState::Idle;
    PendingBrush Pending;
    EntityId PendingEntity = {};
    std::vector<SelectableRef> PendingPreviousSelection;
};
