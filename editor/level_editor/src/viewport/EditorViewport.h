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
    // The camera as seen through the panel's screen rect, which is what the
    // input path projects against.
    [[nodiscard]] CameraRenderData BuildRenderData() const;
    // The camera a render target of this size is rendered through. Not the same
    // thing as the one above during a resize: the target carries the size the
    // panel reported last frame, so for one frame the two disagree, and a view
    // rendered through the panel's rect would come out stretched. Rendering
    // goes through this one, once per view, and every renderer in the view is
    // handed the result.
    [[nodiscard]] CameraRenderData CameraForExtent(uint32_t width, uint32_t height) const;
    [[nodiscard]] bool Contains(ImVec2 point) const;
};
