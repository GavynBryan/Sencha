#pragma once

#include "EditorCamera.h"

#include <math/spatial/GridPlane.h>

#include <imgui.h>

struct EditorViewport
{
    EditorCamera Camera;
    GridPlane ActiveGrid = GridPlanes::XZ();
    ImVec2 RegionMin = {};
    ImVec2 RegionMax = {};
    bool IsActive = false;
    bool WantsFlyCameraInput = false;
    bool WantsOrthoPanInput = false;

    [[nodiscard]] float AspectRatio() const;
    [[nodiscard]] CameraRenderData BuildRenderData() const;
    [[nodiscard]] bool Contains(ImVec2 point) const;
};
