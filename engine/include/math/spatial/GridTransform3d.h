#pragma once

#include <math/Vec.h>
#include <math/geometry/3d/Aabb3d.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

//=============================================================================
// GridTransform3d
//
// World mapping for a 3D lattice of sample points: point (x, y, z) sits at
// Origin + index * CellSize (point convention: Dims points per axis span
// Dims - 1 cells). Pairs with Grid3d<T> for storage; this type owns only the
// world <-> grid mapping and the trilinear weights a sample between points
// needs. Flattened indices follow Grid3d order (z-major, then y, then x).
//=============================================================================
struct GridTransform3d
{
    Vec3d Origin;              // world position of grid point (0, 0, 0)
    float CellSize = 1.0f;     // world units between adjacent points, > 0
    std::uint32_t DimsX = 1;   // grid points per axis, >= 1
    std::uint32_t DimsY = 1;
    std::uint32_t DimsZ = 1;

    std::uint32_t PointCount() const { return DimsX * DimsY * DimsZ; }

    std::uint32_t FlatIndex(std::uint32_t x, std::uint32_t y, std::uint32_t z) const
    {
        return z * DimsX * DimsY + y * DimsX + x;
    }

    Vec3d PointPosition(std::uint32_t x, std::uint32_t y, std::uint32_t z) const
    {
        return Origin + Vec3d(static_cast<float>(x),
                              static_cast<float>(y),
                              static_cast<float>(z)) * CellSize;
    }

    // Continuous grid coordinates of a world position; (0,0,0) at Origin,
    // one unit per cell. Not clamped.
    Vec3d WorldToGrid(const Vec3d& world) const
    {
        return (world - Origin) / CellSize;
    }

    Aabb3d Bounds() const
    {
        return Aabb3d{ Origin,
                       PointPosition(DimsX - 1, DimsY - 1, DimsZ - 1) };
    }

    // The lattice covering `bounds` at `cellSize`: origin at bounds.Min, point
    // counts rounded up so the last point sits at or past bounds.Max. A
    // degenerate axis still gets one cell so the grid remains sampleable.
    static GridTransform3d FromBounds(const Aabb3d& bounds, float cellSize)
    {
        const Vec3d extent = bounds.Size();
        GridTransform3d grid;
        grid.Origin = bounds.Min;
        grid.CellSize = cellSize;
        grid.DimsX = 1 + std::max(1u, static_cast<std::uint32_t>(
            std::ceil(extent.X / cellSize - 1e-4f)));
        grid.DimsY = 1 + std::max(1u, static_cast<std::uint32_t>(
            std::ceil(extent.Y / cellSize - 1e-4f)));
        grid.DimsZ = 1 + std::max(1u, static_cast<std::uint32_t>(
            std::ceil(extent.Z / cellSize - 1e-4f)));
        return grid;
    }
};

//=============================================================================
// TrilinearSample3d
//
// The eight lattice corners bracketing a world position and their trilinear
// blend weights. Corner order is fixed (x fastest, then y, then z:
// 000, 100, 010, 110, 001, 101, 011, 111) so accumulation over the weights is
// deterministic. Positions outside the grid clamp to the boundary cells, the
// same behavior as clamped hardware trilinear sampling.
//=============================================================================
struct TrilinearSample3d
{
    std::uint32_t Corner[8] = {};  // flattened grid indices
    float Weight[8] = {};          // sums to 1
};

inline TrilinearSample3d TrilinearSampleAt(const GridTransform3d& grid,
                                           const Vec3d& world)
{
    const Vec3d gc = grid.WorldToGrid(world);
    const float maxX = static_cast<float>(grid.DimsX - 1);
    const float maxY = static_cast<float>(grid.DimsY - 1);
    const float maxZ = static_cast<float>(grid.DimsZ - 1);
    const float cx = std::clamp(gc.X, 0.0f, maxX);
    const float cy = std::clamp(gc.Y, 0.0f, maxY);
    const float cz = std::clamp(gc.Z, 0.0f, maxZ);

    // The lower corner is capped one cell short of the edge so a position on
    // the far boundary still brackets a full cell (t lands on 1).
    const float fx = std::floor(std::min(cx, std::max(maxX - 1.0f, 0.0f)));
    const float fy = std::floor(std::min(cy, std::max(maxY - 1.0f, 0.0f)));
    const float fz = std::floor(std::min(cz, std::max(maxZ - 1.0f, 0.0f)));
    const std::uint32_t x0 = static_cast<std::uint32_t>(fx);
    const std::uint32_t y0 = static_cast<std::uint32_t>(fy);
    const std::uint32_t z0 = static_cast<std::uint32_t>(fz);
    const std::uint32_t x1 = std::min(x0 + 1, grid.DimsX - 1);
    const std::uint32_t y1 = std::min(y0 + 1, grid.DimsY - 1);
    const std::uint32_t z1 = std::min(z0 + 1, grid.DimsZ - 1);

    const float tx = cx - fx;
    const float ty = cy - fy;
    const float tz = cz - fz;

    TrilinearSample3d sample;
    const std::uint32_t xs[2] = { x0, x1 };
    const std::uint32_t ys[2] = { y0, y1 };
    const std::uint32_t zs[2] = { z0, z1 };
    const float wx[2] = { 1.0f - tx, tx };
    const float wy[2] = { 1.0f - ty, ty };
    const float wz[2] = { 1.0f - tz, tz };
    std::uint32_t out = 0;
    for (std::uint32_t z = 0; z < 2; ++z)
        for (std::uint32_t y = 0; y < 2; ++y)
            for (std::uint32_t x = 0; x < 2; ++x)
            {
                sample.Corner[out] = grid.FlatIndex(xs[x], ys[y], zs[z]);
                sample.Weight[out] = wx[x] * wy[y] * wz[z];
                ++out;
            }
    return sample;
}
