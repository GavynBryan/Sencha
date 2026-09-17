#include <ui/UiSurfacePlacement.h>

std::optional<Vec2d> MapWindowPointToSurface(Vec2d windowPoint,
                                             const Rect2d& windowRect,
                                             RenderExtent surfaceSize)
{
    if (windowRect.Size.X <= 0.0f || windowRect.Size.Y <= 0.0f)
        return std::nullopt;
    // Half-open on the far edges: a point on the right or bottom boundary is
    // outside, the way a pixel grid is addressed.
    const Vec2d local{ windowPoint.X - windowRect.Position.X,
                       windowPoint.Y - windowRect.Position.Y };
    if (local.X < 0.0f || local.Y < 0.0f
        || local.X >= windowRect.Size.X || local.Y >= windowRect.Size.Y)
        return std::nullopt;
    return Vec2d{ local.X * static_cast<float>(surfaceSize.Width) / windowRect.Size.X,
                  local.Y * static_cast<float>(surfaceSize.Height) / windowRect.Size.Y };
}

Vec2d MapSurfacePointToWindow(Vec2d surfacePoint,
                              const Rect2d& windowRect,
                              RenderExtent surfaceSize)
{
    const float sx = surfaceSize.Width == 0 ? 0.0f : windowRect.Size.X / static_cast<float>(surfaceSize.Width);
    const float sy = surfaceSize.Height == 0 ? 0.0f : windowRect.Size.Y / static_cast<float>(surfaceSize.Height);
    return Vec2d{ windowRect.Position.X + surfacePoint.X * sx,
                  windowRect.Position.Y + surfacePoint.Y * sy };
}
