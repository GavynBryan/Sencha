#pragma once

#include "ui/chrome/ChromeBars.h"

#include <imgui.h>

#include <functional>

class ManipulatorSession;
class ToolRegistry;
class MeshEditService;
struct GridSettings;
struct WorldViewSettings;

// The editing toolbar's presentation over state that lives elsewhere. Its
// control groups, each mounted in its own module bay:
//   - the active tool's own contextual controls, which the tool draws itself;
//   - transform: the gizmo (Resize/Move/Rotate/Scale), its space (grid/world/
//     local), and the pivot pair, driving ManipulatorSession;
//   - grid: snap, snap target, zone bounds, spacing, and the grid-frame verbs;
// The tool list itself is the tool palette panel, the mesh element mode is
// the tool properties panel, and the cook/play loop is the workspace bar;
// none of them lives here.
//
// It owns no host. The Perspective viewport reserves a row under its header
// and hands the rect to DrawViewportRow, which seats the tool context at the
// left end, the grid group at the right end, and the gizmo strip on the
// midline the host names, its tail on its left (ToolbarRowPlacement.h has
// the rule).
class EditorToolbar
{
public:
    // Where the bar's channel surface comes from. The shell resolves a theme's
    // finish and artwork once at the frame boundary; the toolbar asks for the
    // result and never learns a texture path.
    using SurfaceProvider = std::function<EditorChrome::BarSurface()>;
    void SetSurfaceProvider(SurfaceProvider provider) { Surface = std::move(provider); }

    // Host wiring for the grid-frame verbs (origin/align/rotate/reset). The
    // toolbar edits spacing and snap directly through GridSettings; frame verbs
    // need scene access, so they stay workspace-side behind callbacks.
    struct GridFrameControls
    {
        std::function<void()> OriginToSelection;
        std::function<void()> AlignToFace;
        std::function<void()> RotateInPlane; // one 90 degree step
        std::function<void()> Reset;
        std::function<void()> ToggleMoveOrigin; // Move gizmo drags the grid origin
        std::function<bool()> IsMovingOrigin;
    };

    // Host wiring for the transform group. The session drives gizmo mode/space
    // and the pivot toggles; the SetOrigin* callbacks re-origin the primary
    // brush (pivot commit, first selected vertex, world-bounds center/min
    // corner); HasSelection gates the pivot pair's visibility.
    struct TransformControls
    {
        std::function<void()> SetOriginToPivot;
        std::function<void()> SetOriginToVertex;
        std::function<void()> SetOriginToBoundsCenter;
        std::function<void()> SetOriginToBoundsCorner;
        std::function<bool()> HasSelection;
    };

    // The registry and session are resolved at call time rather than held: the
    // workspace stands them up during bring-up, after the toolbar is built.
    EditorToolbar(std::function<ToolRegistry*()> tools,
                  std::function<ManipulatorSession*()> session,
                  MeshEditService& meshEdit, GridSettings& grid,
                  WorldViewSettings& worldView);

    void SetGridFrameControls(GridFrameControls controls) { GridFrame = std::move(controls); }
    void SetTransformControls(TransformControls controls) { Transform = std::move(controls); }

    // The row a viewport reserves: the chassis painted over [mn, mx], the
    // groups seated on its lane, the gizmo strip centred on `centerX` (the
    // host's choice: the window's midline). ViewportRowHeight is how tall
    // that row is.
    void DrawViewportRow(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float centerX);
    [[nodiscard]] static float ViewportRowHeight();

private:
    void DrawToolContextGroup(); // edge-cut sub-mode / carve apply-cancel
    // The gizmo strip: the four mode buttons, GizmoStripWidth wide.
    void DrawGizmoStrip(float buttonSize);
    // What precedes the strip in the transform module: the gizmo space and,
    // with a selection, the pivot pair.
    void DrawTransformTail(float buttonSize);
    void DrawGridGroup(float buttonSize);
    [[nodiscard]] static float GizmoStripWidth(float buttonSize);

    [[nodiscard]] ToolRegistry& Tools() const;
    [[nodiscard]] ManipulatorSession* Session() const;

    SurfaceProvider Surface;
    std::function<ToolRegistry*()> ToolsResolver;
    std::function<ManipulatorSession*()> SessionResolver;
    MeshEditService& MeshEdit;
    GridSettings& Grid;
    WorldViewSettings& WorldView;
    GridFrameControls GridFrame;
    TransformControls Transform;
    // The widest the transform tail and the grid group have measured this
    // session. The row is placed before they are drawn, and a block that
    // only grows (the pivot pair appearing) is safely placed by its widest.
    float TailWidth = 0.0f;
    float GridWidth = 0.0f;
    bool RowMeasured = false;
};
