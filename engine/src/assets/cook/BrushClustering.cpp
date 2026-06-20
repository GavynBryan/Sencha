#include <assets/cook/BrushClustering.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace
{
    // floor (not truncation toward zero): a center at -0.1 with cellSize 16 must
    // land in cell -1, not 0, or the grid is asymmetric across the origin.
    Vec3i CellOf(const Vec3d& center, double cellSize)
    {
        return Vec3i{
            static_cast<int>(std::floor(center.X / cellSize)),
            static_cast<int>(std::floor(center.Y / cellSize)),
            static_cast<int>(std::floor(center.Z / cellSize)),
        };
    }

    bool SameCell(const Vec3i& a, const Vec3i& b)
    {
        return a.X == b.X && a.Y == b.Y && a.Z == b.Z;
    }
}

std::vector<BrushCell>
ClusterBrushesIntoCells(std::span<const CookBrushGeometry> brushes, double cellSize)
{
    // A non-positive cell size has no grid; collapse to one cell at the world
    // origin so the function stays total and deterministic. The cvar feeding
    // cellSize is clamped, so this is a guard, not a configured code path.
    const bool hasGrid = cellSize > 0.0;

    struct Assignment { Vec3i Coord; std::size_t Index; };
    std::vector<Assignment> assignments;
    assignments.reserve(brushes.size());

    for (std::size_t i = 0; i < brushes.size(); ++i)
    {
        if (brushes[i].Faces.empty())
            continue; // a brush with no faces contributes no geometry
        const Vec3i coord = hasGrid ? CellOf(brushes[i].WorldBounds.Center(), cellSize)
                                    : Vec3i{};
        assignments.push_back({ coord, i });
    }

    // Sort by coord, then by input index: cells come out ascending in (x, y, z)
    // and brushes keep their input order within a cell. Grouping off a sorted
    // vector (no unordered-container iteration) is what makes the cook output
    // independent of how the caller enumerated brushes.
    std::stable_sort(assignments.begin(), assignments.end(),
        [](const Assignment& a, const Assignment& b)
        {
            if (a.Coord.X != b.Coord.X) return a.Coord.X < b.Coord.X;
            if (a.Coord.Y != b.Coord.Y) return a.Coord.Y < b.Coord.Y;
            if (a.Coord.Z != b.Coord.Z) return a.Coord.Z < b.Coord.Z;
            return a.Index < b.Index;
        });

    std::vector<BrushCell> cells;
    for (std::size_t a = 0; a < assignments.size(); )
    {
        const Vec3i coord = assignments[a].Coord;

        BrushCell cell;
        cell.Coord = coord;
        // Positions are float (Vec3d); compute the lattice corner in double for a
        // clean floor boundary, then narrow. Cell-local rebasing is exactly what
        // keeps these small enough that the narrow costs no usable precision.
        cell.Origin = hasGrid
            ? Vec3d{ static_cast<float>(coord.X * cellSize),
                     static_cast<float>(coord.Y * cellSize),
                     static_cast<float>(coord.Z * cellSize) }
            : Vec3d{};

        std::size_t b = a;
        for (; b < assignments.size() && SameCell(assignments[b].Coord, coord); ++b)
        {
            for (const CookFace& src : brushes[assignments[b].Index].Faces)
            {
                CookFace face;
                face.Material = src.Material;
                face.Triangles.reserve(src.Triangles.size());
                for (const StaticMeshVertex& v : src.Triangles)
                {
                    StaticMeshVertex local = v;
                    local.Position = v.Position - cell.Origin; // cell-local rebase
                    face.Triangles.push_back(local);
                }
                cell.Faces.push_back(std::move(face));
            }
        }

        cells.push_back(std::move(cell));
        a = b;
    }

    return cells;
}
