#pragma once

#include <math/Vec.h>

#include <cstdint>

// What the snap toggle snaps to: grid lines, or a picked geometry element
// (vertex position / edge midpoint / face surface point). Element targets are
// resolved per gesture by the consumers (translate drag, brush-create anchor)
// through PickingService.
enum class SnapTarget : std::uint8_t
{
    Grid = 0,
    Vertex,
    Edge,
    Face,
};

// Editor-wide grid/snap settings: the single source for grid spacing, the
// snap-on/off toggle, and the grid frame (origin + axes). Owned by
// EditorWorkspace and consulted by EditorViewport::GetGrid(), which stamps
// these onto the GridPlane it returns, so every snap consumer (picking,
// translate/bounds manipulators, brush-create) and the grid renderer honor one
// shared setting. Surfaced/edited by the toolbar.
//
// The frame lets the grid be moved and rotated (origin to a vertex, axes to a
// slanted face) so off-axis geometry can be worked on-grid. Perspective
// viewports take the frame wholesale; axis-locked ortho viewports keep their
// orientation axes and take only the origin (see EditorViewport::GetGrid).
struct GridSettings
{
    bool SnapEnabled = true;
    SnapTarget Target = SnapTarget::Grid;
    float Spacing = 1.0f;
    Vec3d Origin = {};
    Vec3d AxisU = { 1.0f, 0.0f, 0.0f };
    Vec3d AxisV = { 0.0f, 0.0f, 1.0f };

    [[nodiscard]] bool HasCustomFrame() const
    {
        const GridSettings defaults;
        return Origin != defaults.Origin || AxisU != defaults.AxisU || AxisV != defaults.AxisV;
    }

    void ResetFrame()
    {
        const GridSettings defaults;
        Origin = defaults.Origin;
        AxisU = defaults.AxisU;
        AxisV = defaults.AxisV;
    }
};
