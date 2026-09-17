#pragma once

#include <graphics/RenderExtent.h>
#include <math/Vec.h>
#include <math/geometry/2d/Rect2d.h>

#include <optional>

//=============================================================================
// UiSurfacePlacement
//
// Where a surface is shown, and the arithmetic that follows from it.
//
// Three coordinate spaces meet here, and the names are the contract. Window
// points are what the platform reports pointer events in, what the window's
// extent is measured in, and what an immediate-mode shell lays out in.
// Surface pixels are a surface's own extent -- the target it is drawn into,
// at whatever size its host chose. Display density enters only through that
// choice of size; this mapping never consults it. A window-destined surface
// with no placement of its own occupies the whole window at one surface pixel
// per point, which is what every windowed host has always had.
//=============================================================================

// Maps a window point into the pixels of a surface presented in `windowRect`.
// Nullopt outside the rect. A 1920-wide surface fitted into an 800-wide rect
// maps 2.4 surface pixels per window point.
[[nodiscard]] std::optional<Vec2d> MapWindowPointToSurface(Vec2d windowPoint,
                                                           const Rect2d& windowRect,
                                                           RenderExtent surfaceSize);

// The inverse, for a host drawing over the presentation: where a surface pixel
// lands in window points. Points outside the surface are extrapolated, which
// is what an outline of an element partly off the surface needs.
[[nodiscard]] Vec2d MapSurfacePointToWindow(Vec2d surfacePoint,
                                            const Rect2d& windowRect,
                                            RenderExtent surfaceSize);
