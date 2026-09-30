#pragma once

#include "tools/ITool.h"
#include "brush/BrushFaceFrame.h"
#include "brush/BrushMesh.h"
#include "brush/BrushOps.h"
#include "brush/CarvePolygon.h"
#include "brush/CarveShape.h"
#include "brush/CarveStatus.h"
#include "overlay/EditorOverlayState.h"
#include "viewport/ViewportDialMath.h"
#include "viewport/ViewportId.h"

#include <ecs/EntityId.h>
#include <math/spatial/GridPlane.h>

#include <cstdint>
#include <optional>
#include <vector>

// What the drag box does to the face. Two axes, not one: the mode chooses the
// operation and the shape chooses the outline it cuts, so adding a shape never
// touches the topology and adding a mode never touches the generators.
enum class FaceCarveMode : std::uint8_t
{
    Polygon,  // cut the shape out of the face
    Quad,     // loop cuts at the box bounds and through the shape's rim vertices, so everything outside the carve stays quads; quad faces only
};

// What a press took hold of.
//
// A box corner folds into a fresh corner-to-corner drag, so it needs no identity
// past the press; the shape handles do, because each writes one parameter while
// the box stays exactly where it is.
enum class CarveHandle : std::uint8_t
{
    None,
    BoxCorner,
    Rise,
    Orientation,
};

struct CarveHandleHit
{
    CarveHandle Kind = CarveHandle::None;
    int Corner = -1; // which corner, for BoxCorner; -1 otherwise
};

// What a viewport button does. The row is built once and both the drawing and
// the hit-testing read these, so a button can never be drawn in one place and
// acted on as another.
enum class CarveButton : std::uint8_t
{
    Confirm,
    Cancel,
    SegmentsDown,
    SegmentsUp,
};

// Carve lifecycle. Invariants: TargetEntity, Original, Frame, and the drag box
// are meaningful exactly while Phase != Idle, and the CommandStack pending-edit
// scope is open for that same span (opened by CaptureFace, closed by Commit and
// RevertAll). Transitions: CaptureFace / BeginPendingAdjust -> Dragging,
// EndDrag -> Pending (or Idle via RevertAll when the box never validated),
// Commit / RevertAll -> Idle.
enum class FaceCarvePhase : std::uint8_t
{
    Idle,
    Dragging,
    Pending,
};

// Face-local topology authoring tool. Polygon mode cuts a shape out of the
// face; Quad mode inserts loop cuts at the box bounds and through the shape's
// rim vertices and lays the shape into the cells they leave, so the faces
// around the carve stay quads (the box itself is the loop cuts alone). Releasing does not
// commit: the preview stays pending until Enter or Apply commits it as one undo
// step. Escape, Cancel, or a tool switch reverts the pending edit.
//
// The drag box is the state and the outline is derived from it, so changing
// mode, shape, a shape parameter or pierce re-previews the box the user already
// drew instead of discarding it.
class FaceCarveTool : public ITool
{
public:
    std::string_view GetId() const override;
    std::string_view GetDisplayName() const override;
    IconId GetIcon() const override;

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
    [[nodiscard]] Shortcut GetShortcut() const override;
    [[nodiscard]] bool UsesTransformGizmo() const override { return false; }

    // Toolbar wiring: Apply/Cancel enable state.
    [[nodiscard]] bool HasPending() const { return Phase != FaceCarvePhase::Idle && PreviewValid; }
    [[nodiscard]] bool CanCommit() const { return HasPending(); }

    [[nodiscard]] FaceCarveMode GetMode() const { return Mode; }
    void SetMode(ToolContext& ctx, FaceCarveMode mode);
    [[nodiscard]] CarveShape GetShape() const { return Shape; }
    void SetShape(ToolContext& ctx, CarveShape shape);
    [[nodiscard]] const CarveShapeParams& GetShapeParams() const { return ShapeParams; }
    void SetShapeParams(ToolContext& ctx, const CarveShapeParams& params);
    [[nodiscard]] bool IsPierceEnabled() const { return Pierce; }
    void SetPierceEnabled(ToolContext& ctx, bool enabled);
    // The segment counts the current box and rise can carry, so the panel can
    // show the range it is clamping to rather than silently correcting.
    [[nodiscard]] CarveShapeLimits ShapeLimits() const;

    // Called by the drag interaction (file-local forwarder in the .cpp), which
    // carries what the press grabbed for exactly as long as the drag lasts.
    void UpdateDrag(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos, CarveHandleHit grabbed);
    void EndDrag(ToolContext& ctx);
    void RevertAll(ToolContext& ctx);

private:
    [[nodiscard]] bool CaptureFace(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos);
    [[nodiscard]] bool BeginPendingAdjust(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos,
                                          int corner);
    // Rebuild the preview from the retained box. Every setting that changes what
    // the box means routes through here, which is why the box survives them.
    void RefreshPreview(ToolContext& ctx);
    void UpdatePolygonPreview(ToolContext& ctx, Vec2d boxMin, Vec2d boxMax);
    void UpdateLoopPreview(ToolContext& ctx, Vec2d boxMin, Vec2d boxMax);
    // Show a kernel's result: the mesh as the preview and the pending commit,
    // or the refusal as the readout with the original mesh back in place.
    void PresentOutcome(ToolContext& ctx, CarveOutcome outcome);
    // The drag box in the quad frame the loop cuts and the rectangle pierce work
    // in. False when the face is not a quad, which those operations require.
    [[nodiscard]] bool BoxInQuadFrame(Vec2d boxMin, Vec2d boxMax, Vec2d& outMin, Vec2d& outMax) const;
    void Commit(ToolContext& ctx);
    // The snapped cursor position in the target face's frame coordinates,
    // clamped to the face bounds. nullopt when the ray misses the drag plane.
    [[nodiscard]] std::optional<Vec2d> CursorUv(ToolContext& ctx, const EditorViewport& viewport,
                                                ImVec2 pos) const;
    [[nodiscard]] Vec2d BoxMin() const;
    [[nodiscard]] Vec2d BoxMax() const;
    void WriteReadout(ToolContext& ctx, Vec2d boxMin, Vec2d boxMax, bool pending) const;
    void WriteBoxHandles(ToolContext& ctx, Vec2d boxMin, Vec2d boxMax) const;
    // How many of the surface's faces the current shape reaches; 1 while the
    // carve stays on the picked face.
    [[nodiscard]] int FacesReached(Vec2d boxMin, Vec2d boxMax) const;
    // Confirm and cancel, pinned over the carve rather than parked on the
    // toolbar: the decision belongs where the geometry the user is judging is.
    void WriteViewportButtons(ToolContext& ctx) const;
    // The row, built once. WriteViewportButtons draws from it and
    // ButtonUnderCursor hit-tests from it, so the drawn count and the tested
    // count cannot drift apart.
    struct ButtonRow
    {
        std::vector<ViewportButton> Buttons;
        std::vector<CarveButton> Roles;
        std::optional<ViewportButtonCaption> Caption;
    };
    [[nodiscard]] ButtonRow BuildButtons() const;
    // The dial, refreshed wherever the preview is. It needs no viewport: only its
    // radius does, and the panel resolves that per frame.
    void WriteShapeHandles(ToolContext& ctx) const;
    // Whether the shape's own handles are showing. They ride on a valid preview,
    // like the corner handles, and only the polygon carve has a shape to turn.
    [[nodiscard]] bool ShapeHandlesApply() const;
    // The dial for the current box in this viewport, or nothing when it is too
    // small, too edge-on, or there is no carve to turn.
    [[nodiscard]] std::optional<ViewportDial::Placement> Dial(const EditorViewport& viewport) const;
    // The rise handle's world position: the springline's midpoint.
    [[nodiscard]] Vec3d RiseHandleWorld() const;
    // The angular stops a turn snaps to, or 0 while the grid snap is off.
    [[nodiscard]] float TurnIncrement(const ToolContext& ctx) const;
    // What the cursor is over. Nearest in pixels, so what highlights, what can be
    // grabbed and what is drawn are one answer rather than three.
    [[nodiscard]] CarveHandleHit HandleUnderCursor(const EditorViewport& viewport, ImVec2 pos) const;
    void DragOrientation(ToolContext& ctx, const EditorViewport& viewport, ImVec2 pos);
    void DragRise(ToolContext& ctx, const EditorViewport& viewport, ImVec2 pos);
    // Which of them the pointer is over, or -1. Shared by hover, click and the
    // drag guard so a press on a button never starts a carve underneath it.
    [[nodiscard]] int ButtonUnderCursor(const EditorViewport& viewport, ImVec2 pos) const;

    FaceCarveMode Mode = FaceCarveMode::Polygon;
    CarveShape Shape = CarveShape::Rectangle;
    CarveShapeParams ShapeParams{};
    bool Pierce = false;
    FaceCarvePhase Phase = FaceCarvePhase::Idle;
    // Whether the current box produced a valid Pending mesh. Can go false while
    // Phase == Pending (e.g. switching to Quad on a face that is not a
    // quad); commit requires it.
    bool PreviewValid = false;
    // Why the last preview was refused, for the readout. Ok while PreviewValid.
    CarveStatus LastStatus = CarveStatus::Ok;
    EntityId TargetEntity = {};   // brush whose live mesh is previewed
    BrushMesh Original;           // pre-carve snapshot: revert source + commit "before"
    BrushMesh Pending;            // repaired carve result: the commit "after"
    std::uint32_t FaceIndex = 0;
    // The opening's faces in Pending, for commit-time selection. Empty when
    // the edit left no face there: a pierce, or a loop cut.
    std::vector<std::uint32_t> PendingCutFaces;
    // The tunnel a pierce opened, so the commit selects exactly the edges it
    // made instead of inferring them from which vertices look new.
    std::vector<std::uint32_t> PendingTunnelWalls;
    BrushFaceFrame Frame{};           // canonical planar workspace, captured at drag start
    // The surface the carve may cross onto: the faces continuing the picked
    // face's plane, named by their corners since the kernels renumber. The
    // picked face is the first.
    std::vector<FaceCorners> Workspace;
    Vec2d FrameMin = {};              // the surface's extent in frame coordinates,
    Vec2d FrameMax = {};              // which is what the drag is clamped to; not containment
    // The quad frame, when the face has one. Only the loop cuts and the
    // rectangle pierce need it; the polygon carve works in Frame.
    std::optional<BrushOps::BrushRectFaceFrame> QuadFrame;
    Transform3f TargetTransform = Transform3f::Identity();
    GridPlane DragPlane{};        // world-space plane for cursor projection + snap
    Vec2d AnchorUv = {};          // press corner, frame coordinates
    Vec2d LastUv = {};            // latest drag corner, frame coordinates
    ViewportId DragViewport = {};
    int HotButton = -1; // the viewport button the pointer is over, or -1
    CarveHandle HotHandle = CarveHandle::None; // the shape handle the pointer is over
};
