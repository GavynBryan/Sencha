#include "authoring/PreviewFraming.h"

#include <ui/UiSurfacePlacement.h>

#include <algorithm>

namespace
{
float ZoomRatio(PreviewZoom zoom)
{
    switch (zoom)
    {
    case PreviewZoom::Half:   return 0.5f;
    case PreviewZoom::Double: return 2.0f;
    case PreviewZoom::Fit:
    case PreviewZoom::Actual: return 1.0f;
    }
    return 1.0f;
}
} // namespace

Rect2d PlacePreview(const Rect2d& content, RenderExtent surface, PreviewZoom zoom, Vec2d pan)
{
    const Vec2d size(static_cast<float>(surface.Width), static_cast<float>(surface.Height));
    if (size.X <= 0.0f || size.Y <= 0.0f)
        return Rect2d(content.Center(), Vec2d(0.0f, 0.0f));
    if (zoom == PreviewZoom::Fit)
    {
        const float ratio = std::min(content.Width() / size.X, content.Height() / size.Y);
        return Rect2d::FromCenterSize(content.Center(), size * ratio);
    }
    return Rect2d::FromCenterSize(content.Center() + pan, size * ZoomRatio(zoom));
}

std::array<Rect2d, 2> SafeAreas(const Rect2d& picture)
{
    const auto inset = [&picture](float fraction) {
        const Vec2d margin = picture.Size * fraction;
        return Rect2d::FromMinMax(picture.Min() + margin, picture.Max() - margin);
    };
    return { inset(0.05f), inset(0.10f) };
}

Rect2d BoxOnPicture(const UiElementBox& box, const Rect2d& picture, RenderExtent surface)
{
    return Rect2d::FromMinMax(MapSurfacePointToWindow(Vec2d(box.X, box.Y), picture, surface),
                              MapSurfacePointToWindow(Vec2d(box.X + box.Width, box.Y + box.Height), picture, surface));
}
