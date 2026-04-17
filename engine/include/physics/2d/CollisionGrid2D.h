#pragma once

#include <math/geometry/2d/Aabb2d.h>
#include <math/spatial/Grid2d.h>
#include <math/Vec.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

//=============================================================================
// TileShape
//
// Collision shape classification for a single grid cell. Kept as a uint8_t
// enum so the collision grid is cache-friendly (one byte per cell).
//
// Slope variants are reserved for phase-2 (platformer extension).
//=============================================================================
enum class TileShape : uint8_t
{
    Empty = 0,
    Solid = 1,
};

//=============================================================================
// CollisionGridConfig
//
// World-space placement parameters for a CollisionGrid2D. Origin is the
// minimum-corner position of cell (0, 0) in world units. TileSize is the
// side length of each square cell.
//=============================================================================
struct CollisionGridConfig
{
    Vec2d Origin   = { 0.0f, 0.0f };
    float TileSize = 1.0f;
};

//=============================================================================
// CollisionGrid2D
//
// A uniform grid of TileShape values used as the static-geometry collision
// source for PhysicsDomain2D. Intentionally decoupled from Tilemap2d — the
// render tilemap owns tile IDs and UVs; this grid owns only collision shapes.
//
// World ↔ cell conversion:
//   col = floor((world.x - Origin.x) / TileSize)
//   row = floor((world.y - Origin.y) / TileSize)
//
// The grid is row-major (matches Grid2d<T>): cell (col, row) is the tile at
// column col (X axis) and row row (Y axis).
//
// Usage:
//   CollisionGrid2D grid(64, 64, { .Origin={0,0}, .TileSize=16.0f });
//   grid.SetCell(3, 5, TileShape::Solid);
//   domain.SetCollisionGrid(&grid);
//=============================================================================
class CollisionGrid2D
{
public:
    CollisionGrid2D() = default;
    CollisionGrid2D(uint32_t cols, uint32_t rows,
                    const CollisionGridConfig& config = {});

    // -- Cell access ----------------------------------------------------------

    TileShape GetCell(int col, int row) const;
    void      SetCell(int col, int row, TileShape shape);

    bool IsInBounds(int col, int row) const;
    bool IsSolid(int col, int row) const;

    // -- Coordinate conversion ------------------------------------------------

    // WorldToCell: maps a world-space point to the containing cell indices.
    // Out-of-bounds results are valid integers (may be negative or >= grid size).
    void   WorldToCell(Vec2d world, int& col, int& row) const;

    // CellBounds: world-space AABB of cell (col, row). Does not check bounds.
    Aabb2d CellBounds(int col, int row) const;

    // -- Dimensions -----------------------------------------------------------

    uint32_t GetCols() const { return Cells.GetWidth(); }
    uint32_t GetRows() const { return Cells.GetHeight(); }

    const CollisionGridConfig& GetConfig() const { return Config; }

    // -- Iteration ------------------------------------------------------------

    // Calls fn(col, row, cellBounds) for every solid cell whose bounds
    // overlap 'region'. The region is clamped to the grid boundary.
    template <typename Fn>
    void ForEachSolidInRegion(const Aabb2d& region, Fn&& fn) const;

private:
    CollisionGridConfig Config;
    Grid2d<TileShape>   Cells;
};

// -----------------------------------------------------------------------------
// ForEachSolidInRegion (template — defined in header)
// -----------------------------------------------------------------------------
template <typename Fn>
void CollisionGrid2D::ForEachSolidInRegion(const Aabb2d& region, Fn&& fn) const
{
    if (Cells.IsEmpty()) return;

    const float inv = 1.0f / Config.TileSize;

    int c0 = static_cast<int>(std::floor((region.Min.X - Config.Origin.X) * inv));
    int r0 = static_cast<int>(std::floor((region.Min.Y - Config.Origin.Y) * inv));
    int c1 = static_cast<int>(std::floor((region.Max.X - Config.Origin.X) * inv));
    int r1 = static_cast<int>(std::floor((region.Max.Y - Config.Origin.Y) * inv));

    c0 = std::max(c0, 0);
    r0 = std::max(r0, 0);
    c1 = std::min(c1, static_cast<int>(Cells.GetWidth())  - 1);
    r1 = std::min(r1, static_cast<int>(Cells.GetHeight()) - 1);

    for (int r = r0; r <= r1; ++r)
        for (int c = c0; c <= c1; ++c)
            if (IsSolid(c, r))
                fn(c, r, CellBounds(c, r));
}
