#pragma once

#include "../../tools/ITool.h"
#include "../../brush/BrushMesh.h"
#include "../../brush/BrushOps.h"
#include "../../viewport/ViewportId.h"

#include <ecs/EntityId.h>
#include <math/spatial/GridPlane.h>

// Draw-a-rectangle-on-a-face tool. Hovering highlights flat rectangular quad
// faces; dragging on one sketches a grid-snapped rectangle in the face's plane
// with a live preview of the resolved topology (BrushOps::CarveFaceRect).
// Releasing does NOT commit: the carve stays pending (redraw to replace) until
// Enter / the toolbar Apply commits it as one undo step, after which the new
// center face is selected in Face mode under the Select tool, ready to extrude.
// Escape, the toolbar Cancel, or a tool switch reverts the pending carve.
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

    // Toolbar wiring: Apply/Cancel enable state.
    [[nodiscard]] bool HasPending() const { return PendingValid; }

    // Called by the drag interaction (file-local forwarder in the .cpp).
    void UpdateDrag(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos);
    void EndDrag(ToolContext& ctx);
    void RevertAll(ToolContext& ctx);

private:
    void Commit(ToolContext& ctx);
    // The snapped cursor position in the target face's frame coordinates,
    // clamped to the face. nullopt when the ray misses the drag plane.
    [[nodiscard]] std::optional<Vec2d> CursorUv(ToolContext& ctx, const EditorViewport& viewport,
                                                ImVec2 pos) const;
    void WriteReadout(ToolContext& ctx, Vec2d rectMin, Vec2d rectMax, bool pending) const;

    EntityId TargetEntity = {};   // brush whose live mesh is previewed
    BrushMesh Original;           // pre-carve snapshot: revert source + commit "before"
    BrushMesh Pending;            // repaired carve result: the commit "after"
    bool PendingValid = false;
    bool Dragging = false;
    std::uint32_t FaceIndex = 0;
    BrushOps::BrushRectFaceFrame Frame{}; // local-space frame captured at drag start
    Transform3f TargetTransform = Transform3f::Identity();
    GridPlane DragPlane{};        // world-space plane for cursor projection + snap
    Vec2d AnchorUv = {};          // press corner, frame coordinates
    Vec2d LastUv = {};            // latest drag corner, frame coordinates
    ViewportId DragViewport = {};
};
