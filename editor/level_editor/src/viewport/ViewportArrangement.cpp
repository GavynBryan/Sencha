#include "viewport/ViewportArrangement.h"

#include "viewport/ViewportLayout.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr float kDividerMargin = 0.15f;

Rect2d Tile(float left, float top, float right, float bottom)
{
    const float x0 = std::round(left);
    const float y0 = std::round(top);
    return Rect2d(x0, y0, std::max(0.0f, std::round(right) - x0), std::max(0.0f, std::round(bottom) - y0));
}
} // namespace

std::vector<ViewportId> ViewsShownIn(ViewportArrangementKind kind, const ViewportLayout& layout)
{
    std::vector<ViewportId> shown;
    const EditorViewport* active = layout.Active();
    if (kind == ViewportArrangementKind::Single)
    {
        if (active != nullptr)
            shown.push_back(active->Id);
        return shown;
    }
    if (kind == ViewportArrangementKind::Quad)
    {
        for (const ViewportLayout::ViewportStorage& viewport : layout.All())
            if (viewport != nullptr && shown.size() < TileCount(kind))
                shown.push_back(viewport->Id);
        return shown;
    }

    const EditorViewport* perspective = nullptr;
    const EditorViewport* ortho = nullptr;
    for (const ViewportLayout::ViewportStorage& viewport : layout.All())
    {
        if (viewport == nullptr)
            continue;
        const bool isPerspective = viewport->Orientation == ViewportOrientation::Perspective;
        if (isPerspective && perspective == nullptr)
            perspective = viewport.get();
        else if (!isPerspective && ortho == nullptr)
            ortho = viewport.get();
    }
    if (active != nullptr && active->Orientation != ViewportOrientation::Perspective)
        ortho = active;
    for (const EditorViewport* viewport : { perspective, ortho })
        if (viewport != nullptr)
            shown.push_back(viewport->Id);
    return shown;
}

std::vector<Rect2d> ArrangeViewports(ViewportArrangementKind kind, const Rect2d& content, Vec2d divider, float gutter)
{
    const Vec2d min = content.Min();
    const Vec2d max = content.Max();
    if (kind == ViewportArrangementKind::Single)
        return { Tile(min.X, min.Y, max.X, max.Y) };

    const float half = std::max(0.0f, gutter) * 0.5f;
    const float splitX = min.X + content.Width() * std::clamp(divider.X, kDividerMargin, 1.0f - kDividerMargin);
    if (kind == ViewportArrangementKind::Split)
        return { Tile(min.X, min.Y, splitX - half, max.Y), Tile(splitX + half, min.Y, max.X, max.Y) };

    const float splitY = min.Y + content.Height() * std::clamp(divider.Y, kDividerMargin, 1.0f - kDividerMargin);
    return {
        Tile(min.X, min.Y, splitX - half, splitY - half),
        Tile(splitX + half, min.Y, max.X, splitY - half),
        Tile(min.X, splitY + half, splitX - half, max.Y),
        Tile(splitX + half, splitY + half, max.X, max.Y),
    };
}
