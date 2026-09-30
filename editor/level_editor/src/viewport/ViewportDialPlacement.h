#pragma once

#include "EditorViewport.h"
#include "ViewportDialMath.h"
#include "ViewportProjection.h"

#include "ui/EditorUiStyle.h"

// Resolving a dial against a live viewport. The pure rule in ViewportDialMath
// needs the world size of one pixel and the direction the viewport looks from,
// and the panel that draws a dial and the tool that hit-tests it must arrive at
// exactly the same circle. One function, so they cannot disagree, and the camera
// is read fresh each time so orbiting resizes the dial rather than stranding it.
namespace ViewportDial
{
[[nodiscard]] inline Placement PlaceIn(const EditorViewport& viewport, Vec3d center, Vec3d axisU,
                                       Vec3d axisV, float boxSemiMinor)
{
    const ViewportProjection projection(viewport);
    // The ray through the middle of the region is the view axis in both the
    // perspective and the orthographic modes, which the camera's own forward is
    // not.
    const ImVec2 middle((viewport.RegionMin.x + viewport.RegionMax.x) * 0.5f,
                        (viewport.RegionMin.y + viewport.RegionMax.y) * 0.5f);
    return Place(center, axisU, axisV, projection.RayThroughPixel(middle).Direction, boxSemiMinor,
                 projection.WorldSizeForPixels(center, 1.0f), EditorUi::Px(1.0f));
}
} // namespace ViewportDial
