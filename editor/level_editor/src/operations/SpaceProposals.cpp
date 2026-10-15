#include "SpaceProposals.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <queue>
#include <tuple>

namespace
{
    // A cell of one floor: column, row and floor.
    using CellKey = std::tuple<std::int32_t, std::int32_t, std::int32_t>;

    struct Cell
    {
        float Floor = 0.0f;
        float Depth = 0.0f;
        std::int32_t Owner = -1;
    };

    constexpr std::int32_t kNeighbours[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
                                                 { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };

    CellKey Next(const CellKey& key, int n)
    {
        return { std::get<0>(key) + kNeighbours[n][0], std::get<1>(key) + kNeighbours[n][1], std::get<2>(key) };
    }

    float StepLength(int n, float cell)
    {
        return n < 4 ? cell : cell * 1.41421356f;
    }

    // Whether `p` lies in the convex outline, seen from above, either winding.
    bool Inside(std::span<const Vec3d> outline, float x, float z)
    {
        bool positive = false;
        bool negative = false;
        for (std::size_t i = 0; i < outline.size(); ++i)
        {
            const Vec3d& a = outline[i];
            const Vec3d& b = outline[(i + 1) % outline.size()];
            const float cross = (b.X - a.X) * (z - a.Z) - (b.Z - a.Z) * (x - a.X);
            positive = positive || cross > 1e-6f;
            negative = negative || cross < -1e-6f;
        }
        return !(positive && negative);
    }

    std::map<CellKey, Cell> Rasterize(const NavMeshSnapshot& snapshot, std::span<const std::uint32_t> polygons,
                                      const SpaceProposalSettings& settings)
    {
        std::map<CellKey, Cell> cells;
        for (const std::uint32_t index : polygons)
        {
            const NavSnapshotPolygon& polygon = snapshot.Polygons[index];
            const std::span<const Vec3d> outline(snapshot.Vertices.data() + polygon.FirstVertex, polygon.VertexCount);
            Vec3d low = outline.front();
            Vec3d high = outline.front();
            float floor = 0.0f;
            for (const Vec3d& v : outline)
            {
                low = Vec3d{ std::min(low.X, v.X), std::min(low.Y, v.Y), std::min(low.Z, v.Z) };
                high = Vec3d{ std::max(high.X, v.X), std::max(high.Y, v.Y), std::max(high.Z, v.Z) };
                floor += v.Y / static_cast<float>(outline.size());
            }
            const std::int32_t level = static_cast<std::int32_t>(std::floor(floor / settings.FloorSeparation + 0.5f));
            for (auto column = static_cast<std::int32_t>(std::floor(low.X / settings.Cell));
                 column <= static_cast<std::int32_t>(std::floor(high.X / settings.Cell)); ++column)
                for (auto row = static_cast<std::int32_t>(std::floor(low.Z / settings.Cell));
                     row <= static_cast<std::int32_t>(std::floor(high.Z / settings.Cell)); ++row)
                    if (Inside(outline, (static_cast<float>(column) + 0.5f) * settings.Cell,
                               (static_cast<float>(row) + 0.5f) * settings.Cell))
                        cells.emplace(CellKey{ column, row, level }, Cell{ floor });
        }
        return cells;
    }

    // Each cell's distance to the nearest cell that is not walkable.
    void MeasureDepth(std::map<CellKey, Cell>& cells, float size)
    {
        using Entry = std::pair<float, CellKey>;
        std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
        for (auto& [key, cell] : cells)
        {
            cell.Depth = std::numeric_limits<float>::max();
            for (int n = 0; n < 4; ++n)
                if (!cells.contains(Next(key, n)))
                    cell.Depth = 0.5f * size;
            if (cell.Depth < std::numeric_limits<float>::max())
                open.push({ cell.Depth, key });
        }
        while (!open.empty())
        {
            const auto [depth, key] = open.top();
            open.pop();
            if (depth > cells.at(key).Depth)
                continue;
            for (int n = 0; n < 8; ++n)
                if (auto next = cells.find(Next(key, n)); next != cells.end()
                    && depth + StepLength(n, size) < next->second.Depth)
                {
                    next->second.Depth = depth + StepLength(n, size);
                    open.push({ next->second.Depth, next->first });
                }
        }
    }

    // Numbers each connected group of unowned cells `admits` accepts, from
    // `first`; returns one past the last number given.
    template <typename Admits>
    std::int32_t Components(std::map<CellKey, Cell>& cells, std::int32_t first, Admits&& admits)
    {
        std::int32_t owner = first;
        for (auto& [key, cell] : cells)
        {
            if (cell.Owner >= 0 || !admits(cell))
                continue;
            std::vector<CellKey> stack{ key };
            cell.Owner = owner;
            while (!stack.empty())
            {
                const CellKey at = stack.back();
                stack.pop_back();
                for (int n = 0; n < 8; ++n)
                    if (auto next = cells.find(Next(at, n)); next != cells.end() && next->second.Owner < 0 && admits(next->second))
                    {
                        next->second.Owner = owner;
                        stack.push_back(next->first);
                    }
            }
            ++owner;
        }
        return owner;
    }

    // Grows each owner's cells outward over unowned cells for up to `reach` metres.
    void Grow(std::map<CellKey, Cell>& cells, float reach, float size)
    {
        using Entry = std::tuple<float, std::int32_t, CellKey>;
        std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
        std::map<CellKey, float> grown;
        for (const auto& [key, cell] : cells)
            if (cell.Owner >= 0)
                open.push({ 0.0f, cell.Owner, key });
        while (!open.empty())
        {
            const auto [distance, owner, key] = open.top();
            open.pop();
            // Every neighbour one step away, diagonals included, so a
            // rectangular core grows back to a rectangle, corners and all.
            for (int n = 0; n < 8; ++n)
            {
                auto next = cells.find(Next(key, n));
                const float further = distance + size;
                if (next == cells.end() || next->second.Owner >= 0 || further > reach)
                    continue;
                next->second.Owner = owner;
                open.push({ further, owner, next->first });
            }
        }
    }

    std::vector<ProposedSpace> Gather(const std::map<CellKey, Cell>& cells, std::int32_t first, std::int32_t end,
                                      ProposedSpaceKind kind, float minArea, const SpaceProposalSettings& settings)
    {
        std::vector<ProposedSpace> spaces(static_cast<std::size_t>(end - first));
        std::vector<bool> seen(spaces.size(), false);
        const float cellArea = settings.Cell * settings.Cell;
        for (const auto& [key, cell] : cells)
        {
            if (cell.Owner < first || cell.Owner >= end)
                continue;
            ProposedSpace& space = spaces[static_cast<std::size_t>(cell.Owner - first)];
            const Vec3d low{ static_cast<float>(std::get<0>(key)) * settings.Cell, cell.Floor,
                             static_cast<float>(std::get<1>(key)) * settings.Cell };
            const Vec3d high{ low.X + settings.Cell, cell.Floor, low.Z + settings.Cell };
            if (!seen[static_cast<std::size_t>(cell.Owner - first)])
            {
                space = ProposedSpace{ kind, low, high, 0.0f };
                seen[static_cast<std::size_t>(cell.Owner - first)] = true;
            }
            space.Min = Vec3d{ std::min(space.Min.X, low.X), std::min(space.Min.Y, low.Y), std::min(space.Min.Z, low.Z) };
            space.Max = Vec3d{ std::max(space.Max.X, high.X), std::max(space.Max.Y, high.Y), std::max(space.Max.Z, high.Z) };
            space.Area += cellArea;
        }
        std::vector<ProposedSpace> kept;
        for (ProposedSpace& space : spaces)
            if (space.Area >= minArea)
            {
                space.Min = space.Min - Vec3d{ settings.Inset, 0, settings.Inset };
                space.Max = space.Max + Vec3d{ settings.Inset, 0, settings.Inset };
                space.Max.Y = space.Min.Y;
                kept.push_back(space);
            }
        return kept;
    }
}

std::vector<ProposedSpace> ProposeSpaces(const NavMeshSnapshot& snapshot, std::span<const std::uint32_t> polygons,
                                         const SpaceProposalSettings& settings)
{
    std::map<CellKey, Cell> cells = Rasterize(snapshot, polygons, settings);
    MeasureDepth(cells, settings.Cell);
    const std::int32_t rooms = Components(cells, 0, [&](const Cell& cell) { return cell.Depth >= settings.CoreDepth; });
    Grow(cells, settings.CoreDepth, settings.Cell);
    const std::int32_t passages = Components(cells, rooms, [](const Cell&) { return true; });

    std::vector<ProposedSpace> spaces = Gather(cells, 0, rooms, ProposedSpaceKind::Room, settings.MinRoomArea, settings);
    for (ProposedSpace& passage : Gather(cells, rooms, passages, ProposedSpaceKind::Passage, settings.MinPassageArea, settings))
        spaces.push_back(passage);
    return spaces;
}
