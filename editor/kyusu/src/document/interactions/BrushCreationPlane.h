#pragma once

#include <math/geometry/3d/Aabb3d.h>
#include <math/spatial/GridPlane.h>
#include <math/Vec.h>

#include <optional>

// Forward-declared (not <imgui.h>) so the pure depth helpers below stay includable
// from the UI-free brush_tests target. ImVec2 is only used by the resolver, which
// lives in the .cpp where imgui is fully available.
struct ImVec2;
struct ToolContext;
struct EditorViewport;

// Everything a create-drag needs, resolved once from the press: the plane the
// cursor draws on (U/V axes plus an origin that may be lifted onto a surface),
// the snapped anchor, and the fixed extent on the depth axis (the one not drawn
// on). U/V come live from the drag; depth is decided here and held for the drag.
//
// Two shapes of plane exist. A world-aligned plane (every axis a signed world
// axis: the default grids and surface-rest creation) builds the brush with an
// identity transform and world-component extents, exactly as before the grid
// frame existed. A frame plane (the grid moved/rotated off the world axes)
// measures the drag in plane UV space and rotates the brush onto the frame, so
// created brushes align with the working grid. FrameAligned picks the path;
// DepthDir is the unit normal the depth extent runs along (equals the DepthAxis
// world axis in the aligned case).
struct BrushCreationPlane
{
    GridPlane Plane;
    Vec3d     Anchor = {};
    int       DepthAxis = 1;
    Vec3d     DepthDir = { 0.0f, 1.0f, 0.0f };
    float     DepthCenter = 0.0f;
    float     DepthHalf = 0.5f;
    // Side of the rest plane the depth extent grows toward, measured along
    // DepthDir. Kept even for zero-depth Plane drags so a later switch to a
    // solid primitive knows which side to grow.
    float     DepthSign = 1.0f;
    bool      FrameAligned = true;
};

// Depth-axis placement (center, half-extent, rest side). Pure math, no
// viewport/ImGui, so the three creation cases unit-test in brush_tests.
struct BrushDepthPlacement
{
    float Center = 0.0f;
    float Half = 0.0f;
    // Side of the reference (rest) plane the extent grows toward; +1 by
    // convention when there is no natural rest side (depth copied from bounds).
    float Sign = 1.0f;
};

// Rest on a grid plane: bottom on the grid line, one cell tall, extruded toward
// the camera (towardCamera is the depth-axis component of the view direction).
[[nodiscard]] inline BrushDepthPlacement RestOnGridDepth(float gridDepth, float spacing, float towardCamera)
{
    const float half = spacing * 0.5f;
    const float sign = towardCamera >= 0.0f ? 1.0f : -1.0f;
    return BrushDepthPlacement{ .Center = gridDepth + half * sign, .Half = half, .Sign = sign };
}

// Rest flush on a hit surface, extruded one cell along the face's outward normal
// (normalDepth is the depth-axis component of that normal).
[[nodiscard]] inline BrushDepthPlacement RestOnSurfaceDepth(float hitDepth, float spacing, float normalDepth)
{
    const float half = spacing * 0.5f;
    const float sign = normalDepth >= 0.0f ? 1.0f : -1.0f;
    return BrushDepthPlacement{ .Center = hitDepth + half * sign, .Half = half, .Sign = sign };
}

// Match a selected brush's depth-axis bounds (column-match QoL: a new brush
// starts at the same depth as the selected one).
[[nodiscard]] inline BrushDepthPlacement CopyDepthFromBounds(const Aabb3d& bounds, int depthAxis)
{
    const float lo = bounds.Min[depthAxis];
    const float hi = bounds.Max[depthAxis];
    return BrushDepthPlacement{ .Center = (lo + hi) * 0.5f, .Half = (hi - lo) * 0.5f, .Sign = 1.0f };
}

// Depth half-extent when a pending brush is regenerated for a primitive class,
// from placement captured once at drag release. A solid keeps the drag's own
// depth and takes the grid floor only when the drag authored zero depth (a
// Plane drag switched to a solid afterwards); a Plane is a zero-thickness
// sheet regardless of the drag.
[[nodiscard]] inline float PendingDepthHalf(bool solidPrimitive, float dragDepthHalf, float floorHalf)
{
    if (!solidPrimitive)
        return 0.0f;
    return dragDepthHalf > 0.0f ? dragDepthHalf : floorHalf;
}

[[nodiscard]] std::optional<BrushCreationPlane>
ResolveBrushCreationPlane(const ToolContext& ctx, const EditorViewport& viewport, ImVec2 pressPos);
