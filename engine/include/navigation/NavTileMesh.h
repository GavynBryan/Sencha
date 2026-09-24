#pragma once

#include <navigation/NavTileBuild.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

struct NavAnchorAttachment
{
    // Opaque polygon reference within this mesh.
    std::uint64_t Polygon = 0;
    Vec3d Position = Vec3d::Zero();
};

//=============================================================================
// NavTileMesh
//
// One build profile's tiled navigation mesh for one zone: the tiles a
// NavTileBuild produced, resident and linked to their neighbours. A zone owns
// one per profile; the cook builds a temporary one to project link anchors.
//
// Replacing a tile gives it a new revision, so references into the old tile
// stop resolving while references into every other tile stay valid. Mutation
// is owner-thread only and never concurrent with a query.
//=============================================================================
class NavTileMesh
{
public:
    NavTileMesh();
    ~NavTileMesh();
    NavTileMesh(NavTileMesh&&) noexcept;
    NavTileMesh& operator=(NavTileMesh&&) noexcept;
    NavTileMesh(const NavTileMesh&) = delete;
    NavTileMesh& operator=(const NavTileMesh&) = delete;

    // Sizes the mesh for up to maxTiles resident tiles. Fails for an invalid
    // profile or a zero capacity.
    [[nodiscard]] bool Init(const NavBuildProfile& profile, std::uint32_t maxTiles);

    // Loads tile data (a NavTileBuild blob) at its coordinate, replacing any
    // tile already there. Empty data only removes. Returns false when the data
    // is malformed, belongs to another coordinate, or the mesh is full.
    [[nodiscard]] bool SetTile(NavTileCoord tile, std::span<const std::byte> data);

    // The resident tile's revision at a coordinate, or 0 when none is loaded.
    // Changes whenever the tile is replaced or removed.
    [[nodiscard]] std::uint64_t TileRevision(NavTileCoord tile) const;

    [[nodiscard]] const NavBuildProfile& Profile() const;
    [[nodiscard]] std::size_t TileCount() const;
    [[nodiscard]] std::size_t PolygonCount() const;

    // Closest walkable point within halfExtents of point. Allocates a query
    // object per call: a cook and tooling convenience, not a runtime path.
    [[nodiscard]] std::optional<Vec3d> ProjectForTooling(const Vec3d& point,
                                                         const Vec3d& halfExtents) const;

    // Where a navigation link anchor attaches to walkable space, or nullopt
    // when it is out of reach: within the link's entry radius (at least the
    // agent radius) horizontally, and a climb plus an agent height vertically.
    [[nodiscard]] std::optional<NavAnchorAttachment> AttachLinkAnchor(const Vec3d& anchor,
                                                                     float entryRadius) const;

    // Backend state, complete only inside the navigation module.
    struct Backend;
    [[nodiscard]] Backend& GetBackend() { return *Impl; }
    [[nodiscard]] const Backend& GetBackend() const { return *Impl; }

private:
    std::unique_ptr<Backend> Impl;
};
