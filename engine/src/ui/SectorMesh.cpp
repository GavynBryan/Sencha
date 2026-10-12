#include "SectorMesh.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace
{
constexpr float kTurn = 2.0f * std::numbers::pi_v<float>;
// Arc segments no longer than this many pixels.
constexpr float kSegmentLength = 3.0f;
// How far a corner's fringe may reach beyond its edges, as a multiple of the
// fringe: a sharp corner's mitre otherwise runs out to a spike.
constexpr float kMitreLimit = 2.0f;

Vec2d Direction(float angle)
{
    return { std::sin(angle), -std::cos(angle) };
}

int ArcSegments(float sweep, float radius)
{
    return std::clamp(static_cast<int>(std::ceil(sweep * radius / kSegmentLength)), 2, 512);
}

// How far round a flank pulled in by `inset` sits at `radius`.
float FlankOffset(float inset, float radius)
{
    return inset > 0.0f ? std::asin(std::min(1.0f, inset / radius)) : 0.0f;
}

void Arc(Vec2d centre, float radius, float from, float to, int segments, std::vector<Vec2d>& out)
{
    for (int i = 0; i <= segments; ++i)
        out.push_back(centre + Direction(from + (to - from) * static_cast<float>(i) / static_cast<float>(segments))
                                   * radius);
}

// A closed loop's vertices drawn in by half the feather, then pushed out by the
// other half at no coverage, with the fringe between; returns the first drawn-in
// vertex. A hole's fringe runs into the hole.
std::uint32_t AppendLoop(const std::vector<Vec2d>& loop, float feather, bool hole, SectorMesh& mesh)
{
    const std::size_t count = loop.size();
    float area = 0.0f;
    for (std::size_t i = 0; i < count; ++i)
    {
        const Vec2d& a = loop[i];
        const Vec2d& b = loop[(i + 1) % count];
        area += a.X * b.Y - b.X * a.Y;
    }
    const bool outwardIsLeft = (area > 0.0f) == hole;
    std::vector<Vec2d> normals(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        Vec2d edge = loop[(i + 1) % count] - loop[i];
        if (edge.Magnitude() > 1e-6f)
            edge = edge.Normalized();
        normals[i] = outwardIsLeft ? Vec2d(-edge.Y, edge.X) : Vec2d(edge.Y, -edge.X);
    }

    const float half = feather * 0.5f;
    const std::uint32_t inner = static_cast<std::uint32_t>(mesh.Positions.size());
    const std::uint32_t outer = inner + static_cast<std::uint32_t>(count);
    std::vector<Vec2d> mitres(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        Vec2d mitre = (normals[(i + count - 1) % count] + normals[i]) * 0.5f;
        const float length2 = mitre.SqrMagnitude();
        if (length2 > 1e-6f)
            mitre = mitre * std::min(1.0f / length2, kMitreLimit * kMitreLimit);
        mitres[i] = mitre * half;
    }
    for (std::size_t i = 0; i < count; ++i)
    {
        mesh.Positions.push_back(loop[i] - mitres[i]);
        mesh.Coverage.push_back(1.0f);
    }
    for (std::size_t i = 0; i < count; ++i)
    {
        mesh.Positions.push_back(loop[i] + mitres[i]);
        mesh.Coverage.push_back(0.0f);
    }
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const std::uint32_t next = (i + 1) % static_cast<std::uint32_t>(count);
        mesh.Indices.insert(mesh.Indices.end(),
                            { inner + i, inner + next, outer + next, inner + i, outer + next, outer + i });
    }
    return inner;
}

void Quad(SectorMesh& mesh, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d)
{
    mesh.Indices.insert(mesh.Indices.end(), { a, b, c, a, c, d });
}
} // namespace

void AppendSectorMesh(const SectorShape& shape, float feather, SectorMesh& mesh)
{
    const float outer = shape.Outer - shape.Inset;
    const float inner = shape.Inner > 0.0f ? shape.Inner + shape.Inset : 0.0f;
    if (outer <= inner || outer <= 0.0f || shape.Sweep <= 0.0f)
        return;
    const bool full = shape.Sweep >= kTurn - 1e-4f;
    const Vec2d centre = shape.Center;

    if (full)
    {
        const int segments = std::max(12, ArcSegments(kTurn, outer));
        std::vector<Vec2d> rim;
        Arc(centre, outer, 0.0f, kTurn, segments, rim);
        rim.pop_back();
        const std::uint32_t o = AppendLoop(rim, feather, false, mesh);
        const std::uint32_t n = static_cast<std::uint32_t>(rim.size());
        if (inner <= 0.0f)
        {
            const std::uint32_t hub = static_cast<std::uint32_t>(mesh.Positions.size());
            mesh.Positions.push_back(centre);
            mesh.Coverage.push_back(1.0f);
            for (std::uint32_t i = 0; i < n; ++i)
                mesh.Indices.insert(mesh.Indices.end(), { hub, o + i, o + (i + 1) % n });
            return;
        }
        std::vector<Vec2d> hole;
        Arc(centre, inner, 0.0f, kTurn, segments, hole);
        hole.pop_back();
        const std::uint32_t h = AppendLoop(hole, feather, true, mesh);
        for (std::uint32_t i = 0; i < n; ++i)
            Quad(mesh, o + i, o + (i + 1) % n, h + (i + 1) % n, h + i);
        return;
    }

    const float end = shape.Start + shape.Sweep;
    const float outerOffset = FlankOffset(shape.Inset, outer);
    if (outerOffset * 2.0f >= shape.Sweep)
        return;
    const int segments = ArcSegments(shape.Sweep, outer);
    std::vector<Vec2d> loop;
    Arc(centre, outer, shape.Start + outerOffset, end - outerOffset, segments, loop);
    const std::uint32_t arcPoints = static_cast<std::uint32_t>(loop.size());

    if (inner <= 0.0f)
    {
        // A pie's flanks meet at an apex, drawn in along the bisector far
        // enough to keep both inset flanks the inset away.
        const float half = shape.Sweep * 0.5f;
        const float reach = shape.Inset > 0.0f ? std::min(shape.Inset / std::sin(half), outer * 0.5f) : 0.0f;
        loop.push_back(centre + Direction(shape.Start + half) * reach);
        const std::uint32_t base = AppendLoop(loop, feather, false, mesh);
        const std::uint32_t apex = base + arcPoints;
        for (std::uint32_t i = 0; i + 1 < arcPoints; ++i)
            mesh.Indices.insert(mesh.Indices.end(), { apex, base + i, base + i + 1 });
        return;
    }

    const float innerOffset = FlankOffset(shape.Inset, inner);
    if (innerOffset * 2.0f >= shape.Sweep)
        return;
    std::vector<Vec2d> back;
    Arc(centre, inner, shape.Start + innerOffset, end - innerOffset, segments, back);
    loop.insert(loop.end(), back.rbegin(), back.rend());
    const std::uint32_t base = AppendLoop(loop, feather, false, mesh);
    const std::uint32_t last = static_cast<std::uint32_t>(loop.size()) - 1;
    for (std::uint32_t i = 0; i + 1 < arcPoints; ++i)
        Quad(mesh, base + i, base + i + 1, base + last - (i + 1), base + last - i);
}
