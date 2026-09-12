#pragma once

#include "tools/ITool.h"
#include "brush/BrushMesh.h"
#include "viewport/ViewportId.h"

#include <ecs/EntityId.h>
#include <math/geometry/3d/Plane.h>
#include <math/geometry/3d/Transform3d.h>
#include <math/spatial/GridPlane.h>
#include <math/Vec.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

enum class ClipMode : std::uint8_t
{
    KeepFront, // the side the plane's normal points to stays
    KeepBack,
    Split, // both stay, as two brushes
};

// Clip lifecycle. The targets, the gesture and the clip plane are meaningful
// exactly while Phase != Idle, and the CommandStack pending-edit scope is open
// for that same span (opened by BeginDrag, closed by Commit and RevertAll).
enum class ClipPhase : std::uint8_t
{
    Idle,
    Dragging,
    Pending,
};

// Splits the selected brushes along a line drawn in a view. The line stands for
// the plane through it along the normal of the surface it was drawn on: an
// ortho view's grid (its view direction), or in perspective the face under the
// press, so a line on a wall cuts straight through the wall. Once the gesture
// ends the tool owns that plane in world space; the camera can move and the
// pending cut does not.
//
// Two planes, kept apart: the snap plane the endpoints are dragged on, captured
// once at press and never re-picked during the drag, and the clip plane the
// gesture produces. Endpoints are shown in every view and draggable only in the
// one that drew them; a release never discards a gesture, a line too short to
// stand a plane on just waits for a pin to be dragged.
//
// Targets are the selected brushes, deduplicated and captured at press. A brush
// the plane misses is left alone; one whose halves do not validate makes the
// whole operation non-committable, so one gesture never means different things
// to different brushes. Commit leaves the tool active for the next clip.
class ClipTool : public ITool
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
    // A committable clip is placed work and goes into the document; anything
    // less reverts.
    void CommitPending(ToolContext& ctx) override;
    void DrawProperties(ToolContext& ctx) override;
    void DrawToolbarControls(ToolContext& ctx) override;
    [[nodiscard]] Shortcut GetShortcut() const override;
    [[nodiscard]] bool UsesTransformGizmo() const override { return false; }

    [[nodiscard]] ClipMode GetMode() const { return Mode; }
    void SetMode(ToolContext& ctx, ClipMode mode);
    // Whether the cut is closed with a face. Off leaves the halves open along
    // the cut, which is what a shell wants.
    [[nodiscard]] bool IsCapped() const { return Capped; }
    void SetCapped(ToolContext& ctx, bool capped);
    [[nodiscard]] ClipPhase GetPhase() const { return Phase; }
    [[nodiscard]] bool CanCommit() const { return Phase == ClipPhase::Pending && Committable; }
    // The plane the pending cut is made with, once there is one.
    [[nodiscard]] const std::optional<Plane>& GetClipPlane() const { return ClipPlane; }

    // Called by the drag interaction: `endpoint` is 0 or 1 for a grabbed
    // endpoint handle, -1 for a fresh line whose second point follows the cursor.
    void UpdateDrag(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos, int endpoint);
    void EndDrag(ToolContext& ctx, EditorViewport& viewport);
    void RevertAll(ToolContext& ctx);

private:
    enum class Outcome : std::uint8_t
    {
        Missed,  // the plane does not cross the brush; untouched
        Valid,   // both halves validate
        Invalid, // a half did not validate: blocks the whole commit
    };

    struct Target
    {
        EntityId Entity = {};
        BrushMesh Original;
        Transform3f Transform = Transform3f::Identity();
        Outcome Result = Outcome::Missed;
        BrushMesh Front;
        BrushMesh Back;
    };

    // What a press took hold of: an endpoint handle, or nothing.
    [[nodiscard]] int HandleUnderCursor(const EditorViewport& viewport, ImVec2 pos) const;
    [[nodiscard]] int ButtonUnderCursor(const EditorViewport& viewport, ImVec2 pos) const;
    [[nodiscard]] bool CaptureTargets(ToolContext& ctx);
    [[nodiscard]] bool CaptureGesture(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos);
    [[nodiscard]] std::optional<Vec3d> SnappedPoint(ToolContext& ctx, const EditorViewport& viewport,
                                                    ImVec2 pos) const;
    // Rebuild the clip plane from A and B and the captured view, then the preview.
    void RefreshPreview(ToolContext& ctx);
    void RestorePreviews(ToolContext& ctx);
    void Commit(ToolContext& ctx);
    void WriteOverlay(ToolContext& ctx);

    struct ButtonRow;
    [[nodiscard]] ButtonRow BuildButtons() const;

    ClipMode Mode = ClipMode::Split;
    bool Capped = true;
    ClipPhase Phase = ClipPhase::Idle;
    bool Committable = false;
    std::string Status; // why the line cannot be applied, for the readout

    std::vector<Target> Targets;
    std::size_t Crossed = 0;
    std::size_t Missed = 0;
    std::size_t Failed = 0;

    // The gesture, captured at press and owned from then on. The snap plane
    // is where the endpoints live and what the cut stands on: the clip plane
    // is the line plus this plane's normal, in every kind of view.
    ViewportId SourceViewport = {};
    GridPlane SnapPlane{};
    Vec3d A = {};
    Vec3d B = {};
    float Reach = 0.0f; // how far past A and B the construction line is drawn
    std::optional<Plane> ClipPlane;

    int HotButton = -1;
    int HotHandle = -1;
};
