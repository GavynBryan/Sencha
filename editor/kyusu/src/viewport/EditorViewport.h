#pragma once

#include "EditorCamera.h"
#include "GridSettings.h"
#include "viewport/ViewportId.h"
#include "ViewportOrientation.h"
#include "ViewportShading.h"

#include <math/spatial/GridPlane.h>

#include <imgui.h>

struct EditorViewport
{
    EditorCamera Camera;
    ViewportId Id = {};
    ViewportOrientation Orientation = ViewportOrientation::Perspective;
    // How brushes draw in this view. Defaulted from the orientation by
    // ApplyOrientation; left public so a future per-view toggle can override it.
    ViewportShading Shading = ViewportShading::Wireframe;
    ImVec2 RegionMin = {};
    ImVec2 RegionMax = {};
    bool IsActive = false;
    bool WantsFlyCameraInput = false;
    bool WantsOrthoPanInput = false;

    void ApplyOrientation(ViewportOrientation orientation);
    [[nodiscard]] const OrientationTraits& GetOrientationTraits() const;
    // Grid plane for snapping/drawing, with the shared spacing + snap-enable from
    // the editor settings stamped onto the per-orientation plane.
    [[nodiscard]] GridPlane GetGrid(const GridSettings& settings) const;
    [[nodiscard]] const char* GetDisplayLabel() const;
    [[nodiscard]] float AspectRatio() const;
    [[nodiscard]] CameraRenderData BuildRenderData() const;
    [[nodiscard]] bool Contains(ImVec2 point) const;
};
