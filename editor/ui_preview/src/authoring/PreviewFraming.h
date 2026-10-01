#pragma once

#include <math/geometry/2d/Rect2d.h>
#include <graphics/RenderExtent.h>
#include <ui/UiElementInfo.h>

#include <array>
#include <cstdint>

// How large the previewed surface is shown: fitted to the content, or the
// surface's own pixels at a fixed ratio.
enum class PreviewZoom : std::uint8_t
{
    Fit,
    Half,
    Actual,
    Double,
};

// Where the surface shows in `content`, in window pixels: centred at the zoom's
// size, then moved by `pan`. A fitted surface ignores the pan.
[[nodiscard]] Rect2d PlacePreview(const Rect2d& content, RenderExtent surface, PreviewZoom zoom, Vec2d pan);

// The action-safe (5%) and title-safe (10%) areas of a picture.
[[nodiscard]] std::array<Rect2d, 2> SafeAreas(const Rect2d& picture);

// A box in surface pixels, where it shows on the picture.
[[nodiscard]] Rect2d BoxOnPicture(const UiElementBox& box, const Rect2d& picture, RenderExtent surface);
