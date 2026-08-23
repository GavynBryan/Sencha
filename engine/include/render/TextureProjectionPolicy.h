#pragma once

#include <math/Mat.h>
#include <math/geometry/3d/Aabb3d.h>
#include <render/RenderQueue.h>

#include <cstdint>
#include <span>
#include <vector>

//=============================================================================
// Texture-projection policy (pure).
//
// The arithmetic for projecting a texture through an oriented box onto scene
// geometry: the ortho fit that turns a volume and a direction into a
// projector view-projection, the swept bounds of everything the projection
// can touch, the receivers inside them, the screen rect that bounds the
// re-draw, and uniform atlas tiling for projectors that render their own
// source texture per frame. Declared substrate (owner, 2026-08-23) for
// decals and texture-projection effects; docs/renderer/texture-projection.md
// records the pass recipe that consumes it, and commit e18ebe9a holds the
// last full consumer (projected object shadows) for reference. All of it is
// arithmetic over plain values, so all of it is tested without a device.
//=============================================================================

struct ProjectionTileGrid
{
    std::uint32_t TilesPerRow = 1;
    std::uint32_t AtlasExtent = 0; // TilesPerRow * TilePixels
    std::uint32_t TilePixels = 0;
};

// A fixed equal-size grid, ceil(sqrt(maxTiles)) tiles per row -- uniform
// tiles need no allocator, an index is the whole placement.
[[nodiscard]] ProjectionTileGrid MakeProjectionTileGrid(
    std::uint32_t maxTiles, std::uint32_t tilePixels);

struct ProjectionTileRect
{
    std::int32_t X = 0;
    std::int32_t Y = 0;
    std::uint32_t Extent = 0;
};

[[nodiscard]] ProjectionTileRect ProjectionTileRectFor(
    const ProjectionTileGrid& grid, std::uint32_t index);

// uv * xy + zw maps the tile's local [0,1]^2 into the atlas.
[[nodiscard]] Vec4 ProjectionTileUvScaleBias(
    const ProjectionTileGrid& grid, std::uint32_t index);

// The projector's light-space fit: an ortho view-projection fitted around
// the volume looking along the direction, far plane extended by the
// projection reach, a small pad so the projected texture never touches the
// tile border (a clamping sampler would smear a border texel across the
// receiver). DepthRange is the world-unit span the normalized [0,1] depth
// covers -- what converts a world-unit bias into projector depth.
struct ProjectionFit
{
    Mat4 ViewProjection;
    float DepthRange = 0.0f;
};

[[nodiscard]] ProjectionFit FitProjection(const Aabb3d& volume,
                                          const Vec<3>& direction,
                                          float reach);

// World-space bounds of everything the projection can touch: the volume
// swept along its direction. Receivers are gathered against this.
[[nodiscard]] Aabb3d SweptProjectionBounds(const Aabb3d& volume,
                                           const Vec<3>& direction,
                                           float reach);

// Screen-space bounds of the swept volume in one view, clamped to the
// target: the re-draw pass's scissor, so fragments the projection cannot
// reach are never shaded. Width zero means "nothing visible, skip".
// Conservative when the volume crosses the near plane (full target): a
// wrong-but-larger rect costs fill, a wrong-but-smaller one clips the
// projection.
struct ProjectionScreenRect
{
    std::int32_t X = 0;
    std::int32_t Y = 0;
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
};

[[nodiscard]] ProjectionScreenRect ComputeProjectionScreenRect(
    const Aabb3d& sweptBounds,
    const Mat4& cameraViewProjection,
    std::uint32_t targetWidth,
    std::uint32_t targetHeight);

// The union of per-projector rects, for a pass that applies every projector
// in one scissored draw. Empty rects contribute nothing; all-empty unions
// to empty.
[[nodiscard]] ProjectionScreenRect UnionProjectionScreenRects(
    std::span<const ProjectionScreenRect> rects);

// Appends the queue indices of items the swept bounds touch, static items
// only -- an item with a valid SkinnedMesh deforms under a static projector,
// so a projected texture would swim on it. Queue order, capped; returns how
// many intersecting items the cap excluded.
std::uint32_t GatherProjectionReceivers(std::span<const RenderQueueItem> items,
                                        const Aabb3d& sweptBounds,
                                        std::uint32_t cap,
                                        std::vector<std::uint32_t>& outIndices);
