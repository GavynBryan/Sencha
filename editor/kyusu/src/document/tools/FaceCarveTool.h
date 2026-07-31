#pragma once

#include "tools/ITool.h"
#include "brush/BrushMesh.h"
#include "brush/BrushOps.h"
#include "viewport/ViewportId.h"

#include <ecs/EntityId.h>
#include <math/spatial/GridPlane.h>

#include <cstdint>
#include <optional>
#include <vector>

enum class FaceCarveMode : std::uint8_t
{
    Inset,
    EdgeLoop,
};

// Carve lifecycle. Invariants: TargetEntity, Original, Frame, and the drag rect
// are meaningful exactly while Phase != Idle, and the CommandStack pending-edit
// scope is open for that same span (opened by CaptureFace, closed by Commit and
// RevertAll). Transitions: CaptureFace / BeginPendingAdjust -> Dragging,
// EndDrag -> Pending (or Idle via RevertAll when the rect never validated),
// Commit / RevertAll -> Idle.
enum class FaceCarvePhase : std::uint8_t
{
    Idle,
    Dragging,
    Pending,
};

// Face-local topology authoring tool. Inset mode carves an inset face and
// EdgeLoop mode inserts loop cuts at the rectangle bounds. Releasing does not commit: the preview stays
// pending until Enter or Apply commits it as one undo step. Escape, Cancel, or a
// tool switch reverts the pending edit.
class FaceCarveTool : public ITool
{
public:
    std::string_view GetId() const override;
    std::string_view GetDisplayName() const override;
    std::string_view GetIcon() const override;

    InputConsumed OnHover(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos) override;
    void OnHoverEnd(ToolContext& ctx) override;
    InputConsumed OnClick(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override;
    std::unique_ptr<IInteraction> BeginDrag(ToolContext& ctx, EditorViewport& viewport,
                                            const PointerEvent& pressPointer) override;
    InputConsumed OnKeyDown(ToolContext& ctx, const KeyDownEvent& event) override;
    void OnDeactivate(ToolContext& ctx) override;
    void OnCancel(ToolContext& ctx) override;
    // A carve that is far enough along to commit is placed work and goes into
    // the document; a half-formed draft cannot be committed, so it reverts.
    void CommitPending(ToolContext& ctx) override;
    void DrawProperties(ToolContext& ctx) override;
    void DrawToolbarControls(ToolContext& ctx) override;
    [[nodiscard]] Shortcut GetShortcut() const override;

    // Toolbar wiring: Apply/Cancel enable state.
    [[nodiscard]] bool HasPending() const { return Phase != FaceCarvePhase::Idle && PreviewValid; }
    [[nodiscard]] bool CanCommit() const { return HasPending(); }

    [[nodiscard]] FaceCarveMode GetMode() const { return Mode; }
    void SetMode(ToolContext& ctx, FaceCarveMode mode);
    [[nodiscard]] bool IsPierceEnabled() const { return Pierce; }
    void SetPierceEnabled(ToolContext& ctx, bool enabled);

    // Called by the drag interaction (file-local forwarder in the .cpp).
    void UpdateDrag(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos);
    void EndDrag(ToolContext& ctx);
    void RevertAll(ToolContext& ctx);

private:
    [[nodiscard]] bool CaptureFace(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos);
    [[nodiscard]] bool BeginPendingAdjust(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos);
    void UpdateRectPreview(ToolContext& ctx, Vec2d rectMin, Vec2d rectMax);
    void UpdateLoopPreview(ToolContext& ctx, Vec2d rectMin, Vec2d rectMax);
    void Commit(ToolContext& ctx);
    // The snapped cursor position in the target face's frame coordinates,
    // clamped to the face. nullopt when the ray misses the drag plane.
    [[nodiscard]] std::optional<Vec2d> CursorUv(ToolContext& ctx, const EditorViewport& viewport,
                                                ImVec2 pos) const;
    void WriteReadout(ToolContext& ctx, Vec2d rectMin, Vec2d rectMax, bool pending) const;
    void WriteRectHandles(ToolContext& ctx, Vec2d rectMin, Vec2d rectMax) const;

    FaceCarveMode Mode = FaceCarveMode::Inset;
    bool Pierce = false;
    FaceCarvePhase Phase = FaceCarvePhase::Idle;
    // Whether the current rect produced a valid Pending mesh. Can go false
    // while Phase == Pending (e.g. toggling Pierce onto a rect the
    // through-carve rejects); commit requires it.
    bool PreviewValid = false;
    EntityId TargetEntity = {};   // brush whose live mesh is previewed
    BrushMesh Original;           // pre-carve snapshot: revert source + commit "before"
    BrushMesh Pending;            // repaired carve result: the commit "after"
    std::uint32_t FaceIndex = 0;
    BrushOps::BrushRectFaceFrame Frame{}; // local-space frame captured at drag start
    Transform3f TargetTransform = Transform3f::Identity();
    GridPlane DragPlane{};        // world-space plane for cursor projection + snap
    Vec2d AnchorUv = {};          // press corner, frame coordinates
    Vec2d LastUv = {};            // latest drag corner, frame coordinates
    ViewportId DragViewport = {};
};
