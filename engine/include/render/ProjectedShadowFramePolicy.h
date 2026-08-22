#pragma once

#include <math/Mat.h>
#include <math/geometry/3d/Aabb3d.h>
#include <render/ProjectedShadowTypes.h>
#include <render/RenderQueue.h>

#include <cstdint>
#include <span>
#include <vector>

//=============================================================================
// Projected-shadow frame policy (pure).
//
// Everything between "these casters exist" and "record these draws": which
// casters fit the budget for a view, which silhouette tile each one owns,
// the light-space projection its silhouette is rendered and re-projected
// through, and which opaque items receive it. All of it is arithmetic over
// plain values, so all of it is tested without a device.
//=============================================================================

struct ProjectedShadowBudgets
{
    // Casters per frame; the rest drop, farthest first, and the drop is
    // counted rather than silent.
    std::uint32_t MaxCasters = 16;
    // Silhouette tile edge in pixels. The atlas is a fixed equal-size grid,
    // ceil(sqrt(MaxCasters)) tiles per row -- uniform tiles need no
    // allocator, an index is the whole placement.
    std::uint32_t TilePixels = 256;
    // Silhouette blur reach in atlas texels (separable pass over the tiles
    // before projection samples them); 0 leaves the coverage sharp. Capped
    // well inside the projection fit's border padding so the blur cannot
    // pull a neighbouring tile's silhouette into this one.
    float SoftnessTexels = 3.0f;
    // Receiver re-draws per caster; past the cap the shadow fades out on
    // whatever was not drawn, bounded rather than surprising.
    std::uint32_t MaxReceiversPerCaster = 24;
    // How far the shadow projects along its direction, in world units.
    float MaxDistance = 6.0f;
};

// Ranks casters nearest-to-view first (RenderEntityKey ties, so the order is
// stable when distances agree), truncates to the budget, and returns how many
// were dropped.
std::uint32_t RankAndClampProjectedCasters(ProjectedShadowSet& set,
                                           const Vec<3>& viewOrigin,
                                           std::uint32_t maxCasters);

struct ProjectedShadowTileGrid
{
    std::uint32_t TilesPerRow = 1;
    std::uint32_t AtlasExtent = 0; // TilesPerRow * TilePixels
    std::uint32_t TilePixels = 0;
};

[[nodiscard]] ProjectedShadowTileGrid MakeProjectedShadowTileGrid(
    std::uint32_t maxCasters, std::uint32_t tilePixels);

struct ProjectedShadowTileRect
{
    std::int32_t X = 0;
    std::int32_t Y = 0;
    std::uint32_t Extent = 0;
};

[[nodiscard]] ProjectedShadowTileRect ProjectedShadowTileRectFor(
    const ProjectedShadowTileGrid& grid, std::uint32_t index);

// uv * xy + zw maps the tile's local [0,1]^2 into the atlas.
[[nodiscard]] Vec4 ProjectedShadowTileUvScaleBias(
    const ProjectedShadowTileGrid& grid, std::uint32_t index);

// The ortho view-projection the caster's silhouette is rendered through and
// receivers re-project with: fitted around the caster's bounds looking along
// its direction, far plane extended by the projection distance, a small pad
// so the silhouette never touches the tile border (the sampler clamps, and a
// border texel of shadow would smear across the receiver).
[[nodiscard]] Mat4 MakeProjectedShadowViewProjection(
    const ProjectedShadowCaster& caster, float maxDistance);

// World-space bounds of everything the shadow can touch: the caster's bounds
// swept along its direction. Receivers are gathered against this.
[[nodiscard]] Aabb3d ProjectedShadowSweptBounds(
    const ProjectedShadowCaster& caster, float maxDistance);

// Screen-space bounds of the swept volume in one view, clamped to the
// target: the projection pass's scissor, so fragments the shadow cannot
// reach are never shaded. Width zero means "nothing visible, skip".
// Conservative when the volume crosses the near plane (full target): a
// wrong-but-larger rect costs fill, a wrong-but-smaller one clips shadow.
struct ProjectedShadowScreenRect
{
    std::int32_t X = 0;
    std::int32_t Y = 0;
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
};

[[nodiscard]] ProjectedShadowScreenRect ComputeProjectedShadowScreenRect(
    const Aabb3d& sweptBounds,
    const Mat4& cameraViewProjection,
    std::uint32_t targetWidth,
    std::uint32_t targetHeight);

// Appends the queue indices of items the swept bounds touch, static items
// only -- an item with a valid SkinnedMesh is a caster-class object, and a
// grounding blob multiplied onto another character reads as dirt, not
// shadow. Queue order, capped; returns how many intersecting items the cap
// excluded.
std::uint32_t GatherProjectedShadowReceivers(std::span<const RenderQueueItem> items,
                                             const Aabb3d& sweptBounds,
                                             std::uint32_t cap,
                                             std::vector<std::uint32_t>& outIndices);
