#pragma once

#include "EditorCamera.h"
#include "ViewportId.h"
#include "ViewportOrientation.h"

#include <math/spatial/GridPlane.h>

#include <imgui.h>

struct EditorViewport
{
    EditorCamera Camera;
    ViewportId Id = {};
    ViewportOrientation Orientation = ViewportOrientation::Perspective;
    ImVec2 RegionMin = {};
    ImVec2 RegionMax = {};
    bool IsActive = false;
    bool WantsFlyCameraInput = false;
    bool WantsOrthoPanInput = false;

    void ApplyOrientation(ViewportOrientation orientation);
    [[nodiscard]] const OrientationTraits& GetOrientationTraits() const;
    [[nodiscard]] GridPlane GetGrid() const;
    [[nodiscard]] const char* GetDisplayLabel() const;
    [[nodiscard]] float AspectRatio() const;
    [[nodiscard]] CameraRenderData BuildRenderData() const;
    [[nodiscard]] bool Contains(ImVec2 point) const;
};
