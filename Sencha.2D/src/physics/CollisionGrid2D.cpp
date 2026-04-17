#include <physics/CollisionGrid2D.h>

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

CollisionGrid2D::CollisionGrid2D(uint32_t cols, uint32_t rows,
                                 const CollisionGridConfig& config)
    : Config(config)
    , Cells(cols, rows, TileShape::Empty)
{
}

// ---------------------------------------------------------------------------
// Cell access
// ---------------------------------------------------------------------------

TileShape CollisionGrid2D::GetCell(int col, int row) const
{
    if (!IsInBounds(col, row)) return TileShape::Empty;
    return Cells.Get(static_cast<uint32_t>(col), static_cast<uint32_t>(row));
}

void CollisionGrid2D::SetCell(int col, int row, TileShape shape)
{
    if (!IsInBounds(col, row)) return;
    Cells.Set(static_cast<uint32_t>(col), static_cast<uint32_t>(row), shape);
}

bool CollisionGrid2D::IsInBounds(int col, int row) const
{
    return col >= 0 && row >= 0
        && static_cast<uint32_t>(col) < Cells.GetWidth()
        && static_cast<uint32_t>(row) < Cells.GetHeight();
}

bool CollisionGrid2D::IsSolid(int col, int row) const
{
    return GetCell(col, row) != TileShape::Empty;
}

// ---------------------------------------------------------------------------
// Coordinate conversion
// ---------------------------------------------------------------------------

void CollisionGrid2D::WorldToCell(Vec2d world, int& col, int& row) const
{
    const float inv = 1.0f / Config.TileSize;
    col = static_cast<int>(std::floor((world.X - Config.Origin.X) * inv));
    row = static_cast<int>(std::floor((world.Y - Config.Origin.Y) * inv));
}

Aabb2d CollisionGrid2D::CellBounds(int col, int row) const
{
    Vec2d min = {
        Config.Origin.X + static_cast<float>(col) * Config.TileSize,
        Config.Origin.Y + static_cast<float>(row) * Config.TileSize,
    };
    Vec2d max = {
        min.X + Config.TileSize,
        min.Y + Config.TileSize,
    };
    return Aabb2d::FromMinMax(min, max);
}
