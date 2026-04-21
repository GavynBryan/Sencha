#include "EditorViewport.h"

void EditorViewport::ApplyOrientation(ViewportOrientation orientation)
{
    Orientation = orientation;

    const OrientationTraits& traits = Traits(orientation);
    Camera.ActiveMode = traits.Mode;

    if (!traits.UsesCameraAxis)
        Camera.OrthoAxis = traits.OrthoAxis;
}

const OrientationTraits& EditorViewport::GetOrientationTraits() const
{
    return Traits(Orientation);
}

GridPlane EditorViewport::GetGrid() const
{
    const OrientationTraits& traits = GetOrientationTraits();
    if (traits.UsesCameraAxis)
        return GridForAxis(Camera.OrthoAxis);
    return traits.Grid;
}

const char* EditorViewport::GetDisplayLabel() const
{
    return GetOrientationTraits().Label;
}

float EditorViewport::AspectRatio() const
{
    const float width = RegionMax.x - RegionMin.x;
    const float height = RegionMax.y - RegionMin.y;
    if (width <= 0.0f || height <= 0.0f)
        return 1.0f;
    return width / height;
}

CameraRenderData EditorViewport::BuildRenderData() const
{
    return Camera.BuildRenderData(AspectRatio());
}

bool EditorViewport::Contains(ImVec2 point) const
{
    return point.x >= RegionMin.x
        && point.x <= RegionMax.x
        && point.y >= RegionMin.y
        && point.y <= RegionMax.y;
}
