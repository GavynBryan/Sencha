#pragma once

#include <math/Vec.h>
#include <math/geometry/3d/Aabb3d.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

//=============================================================================
// Navigation tile building
//
// The one path from collision triangles to a navigation tile. The navigation
// cook uses it for every tile of a zone, and the runtime uses it to rebuild the
// tiles a geometry change touches, so both produce identical tiles for
// identical input. The signature is backend-neutral: callers never see the
// builder library, only positions, indices, and an opaque tile blob.
//
// Tiles lie on a world-anchored grid: tile (x, z) covers
// [x * TileWorldSize, (x + 1) * TileWorldSize) on each horizontal axis, so a
// cooked tile and a runtime-rebuilt tile with the same coordinates are the
// same region of space. World space is Y-up.
//=============================================================================

// The agent geometry a navmesh variant is built for. Changing any of these
// changes which space exists, so it requires rebuilding that variant's tiles.
struct NavBuildProfile
{
    float Radius = 0.3f;
    float Height = 1.8f;
    float MaxSlopeDegrees = 45.0f;
    // The highest ledge or step an agent walks up or down without a link.
    float MaxClimb = 0.35f;
    // Horizontal and vertical voxel size. Smaller is more precise and slower.
    float CellSize = 0.15f;
    float CellHeight = 0.1f;
    // Tile edge length in cells, integral so every tile covers the same square.
    // The default is sized for runtime rebuild cost; the measurement is in
    // docs/plans/navigation-core.md.
    std::uint32_t TileCells = 32;

    [[nodiscard]] float TileWorldSize() const
    {
        return static_cast<float>(TileCells) * CellSize;
    }
};

inline constexpr std::uint32_t kNavMinTileCells = 8;
inline constexpr std::uint32_t kNavMaxTileCells = 1024;

// Signed tile coordinates on the horizontal plane (x along X, z along Z).
struct NavTileCoord
{
    std::int32_t X = 0;
    std::int32_t Z = 0;

    friend bool operator==(const NavTileCoord&, const NavTileCoord&) = default;
    friend auto operator<=>(const NavTileCoord&, const NavTileCoord&) = default;
};

// Area index 0 is the default area. Indices 1 through kNavMaxAuthoredAreas name
// authored areas, in the order the navigation settings list them.
inline constexpr std::uint8_t kNavDefaultArea = 0;
inline constexpr std::uint8_t kNavMaxAuthoredAreas = 62;

// A convex prism that classifies the walkable surface inside it: a horizontal
// footprint (Y ignored) extruded between MinY and MaxY.
struct NavAreaVolume
{
    std::vector<Vec3d> Footprint;
    float MinY = 0.0f;
    float MaxY = 0.0f;
    std::uint8_t Area = kNavDefaultArea;
};

struct NavTileBuildInput
{
    NavBuildProfile Profile;
    NavTileCoord Tile;
    // World-space triangle soup. The builder only rasterizes triangles that
    // overlap the tile's border-expanded bounds, so callers may pass more
    // than the tile needs.
    std::span<const Vec3d> Positions;
    std::span<const std::uint32_t> Indices;
    std::span<const NavAreaVolume> Areas;
    // Vertical extent of the build volume; nothing outside it is walkable.
    float MinY = 0.0f;
    float MaxY = 0.0f;
};

enum class NavTileBuildResult : std::uint8_t
{
    // Tile holds at least one walkable polygon.
    Built,
    // Input was valid but produced no walkable polygon.
    Empty,
    // Invalid profile or input, or the builder ran out of a fixed limit.
    Failed,
};

// Builds one tile into an opaque blob that NavTileMesh can load. The same
// input always produces the same bytes.
[[nodiscard]] NavTileBuildResult BuildNavTile(const NavTileBuildInput& input,
                                              std::vector<std::byte>& tileData);

// The tile containing a world-space point.
[[nodiscard]] NavTileCoord NavTileAt(const NavBuildProfile& profile, const Vec3d& point);

// A tile's horizontal square widened by the border the builder rasterizes
// beyond it, so walkable area erodes correctly at tile edges.
[[nodiscard]] Aabb3d NavTileBuildBounds(const NavBuildProfile& profile,
                                        NavTileCoord tile, float minY, float maxY);

// Inclusive range of tiles whose build bounds reach `bounds` horizontally:
// every tile that geometry inside `bounds` can change.
struct NavTileRange
{
    NavTileCoord Min;
    NavTileCoord Max;
};
[[nodiscard]] NavTileRange NavTilesTouching(const NavBuildProfile& profile,
                                            const Aabb3d& bounds);

// Whether a triangle reaches a tile's build bounds horizontally.
[[nodiscard]] bool NavTriangleTouchesTile(const Aabb3d& tileBuildBounds,
                                          const Vec3d& a, const Vec3d& b, const Vec3d& c);

// Whether a profile's parameters can build a navmesh at all.
[[nodiscard]] bool IsValidNavBuildProfile(const NavBuildProfile& profile);
