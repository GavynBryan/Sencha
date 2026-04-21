#include "EditorViewport.h"

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
