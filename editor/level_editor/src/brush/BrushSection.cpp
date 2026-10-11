#include "BrushSection.h"

#include "BrushOps.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace
{
    std::uint64_t EdgeKey(std::uint32_t from, std::uint32_t to)
    {
        return (static_cast<std::uint64_t>(from) << 32) | to;
    }
}

std::vector<BrushSectionContour> SectionBrushAtHeight(const BrushMesh& mesh,
                                                      const Transform3f& toWorld, float height)
{
    BrushMesh world = mesh;
    for (BrushVertex& vertex : world.Vertices)
        vertex.Position = toWorld.TransformPoint(vertex.Position);

    // Clipped open, the part below the plane keeps the cut as its boundary;
    // every boundary edge lying on the plane is a piece of the section.
    const Plane cut = Plane::FromNormalAndDistance({ 0.0f, 1.0f, 0.0f }, -height);
    const BrushMesh below = BrushOps::Clip(world, cut, false, BrushOps::ClipCap::Open);
    if (below.Faces.empty())
        return {};

    std::unordered_set<std::uint64_t> directed;
    for (const BrushFace& face : below.Faces)
        for (std::size_t i = 0; i < face.Loop.size(); ++i)
            directed.insert(EdgeKey(face.Loop[i], face.Loop[(i + 1) % face.Loop.size()]));

    const auto onPlane = [&](std::uint32_t index) {
        return std::abs(below.Vertices[index].Position.Y - height) <= BrushOps::kClipSnap;
    };

    // A boundary edge has no reverse twin. Section edges keep the faces'
    // winding, so each one's end is the next one's start.
    std::unordered_map<std::uint32_t, std::uint32_t> next;
    std::unordered_set<std::uint32_t> hasPredecessor;
    for (const BrushFace& face : below.Faces)
        for (std::size_t i = 0; i < face.Loop.size(); ++i)
        {
            const std::uint32_t from = face.Loop[i];
            const std::uint32_t to = face.Loop[(i + 1) % face.Loop.size()];
            if (directed.contains(EdgeKey(to, from)) || !onPlane(from) || !onPlane(to))
                continue;
            next.emplace(from, to);
            hasPredecessor.insert(to);
        }

    const auto ground = [&](std::uint32_t index) {
        const Vec3d& p = below.Vertices[index].Position;
        return Vec2d{ p.X, p.Z };
    };

    std::vector<BrushSectionContour> contours;
    std::unordered_set<std::uint32_t> visited;
    const auto walk = [&](std::uint32_t start) {
        BrushSectionContour contour;
        std::uint32_t at = start;
        while (!visited.contains(at))
        {
            visited.insert(at);
            contour.Points.push_back(ground(at));
            const auto it = next.find(at);
            if (it == next.end())
            {
                contour.Closed = false;
                break;
            }
            at = it->second;
        }
        if (contour.Points.size() >= 2)
            contours.push_back(std::move(contour));
    };

    // Starts in vertex order, so the same brush always answers the same way:
    // open runs first, from the ends nothing leads into, then the cycles.
    std::vector<std::uint32_t> starts;
    starts.reserve(next.size());
    for (const auto& [from, to] : next)
        starts.push_back(from);
    std::ranges::sort(starts);
    for (const std::uint32_t from : starts)
        if (!hasPredecessor.contains(from) && !visited.contains(from))
            walk(from);
    for (const std::uint32_t from : starts)
        if (!visited.contains(from))
            walk(from);
    return contours;
}
