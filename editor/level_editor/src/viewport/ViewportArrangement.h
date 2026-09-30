#pragma once

#include "viewport/ViewportId.h"

#include <math/geometry/2d/Rect2d.h>

#include <cstddef>
#include <cstdint>
#include <vector>

// How the content area is divided among views: one full-bleed view, two side by
// side, or four in a grid. Fixed arrangements, not docking: the only thing a
// person moves is the divider.
enum class ViewportArrangementKind : std::uint8_t
{
    Single,
    Split,
    Quad,
};

[[nodiscard]] constexpr std::size_t TileCount(ViewportArrangementKind kind)
{
    switch (kind)
    {
    case ViewportArrangementKind::Split: return 2;
    case ViewportArrangementKind::Quad:  return 4;
    case ViewportArrangementKind::Single:
    default:                             return 1;
    }
}

class ViewportLayout;

// Which views `kind` shows, in slot order. One shows the active view; two show
// the perspective view beside the active ortho view (the first ortho view
// while perspective is active); four show the layout's first four.
[[nodiscard]] std::vector<ViewportId> ViewsShownIn(ViewportArrangementKind kind, const ViewportLayout& layout);

// The tiles `kind` divides `content` into, in slot order: Split is left then
// right, Quad is row by row from the top left. `divider` is where the tiles
// meet as a fraction of the content (x for Split, both for Quad), kept clear of
// the edges; `gutter` separates neighbouring tiles. Tiles land on whole window
// pixels, so an image drawn into one is never resampled.
[[nodiscard]] std::vector<Rect2d> ArrangeViewports(ViewportArrangementKind kind, const Rect2d& content,
                                                   Vec2d divider, float gutter);
