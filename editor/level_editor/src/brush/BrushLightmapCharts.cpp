#include "BrushLightmapCharts.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <queue>
#include <vector>

namespace
{
    // World-space Newell vector of a face: direction is the face normal,
    // magnitude is twice the polygon area, so summing these across a chart
    // area-weights the chart normal for free.
    Vec3d WorldNewell(const BrushMesh& mesh, const Transform3f& transform, const BrushFace& face)
    {
        // Start at a geometry-derived loop vertex, not the authored loop's
        // arbitrary first entry. Convex tessellation fans from that entry, so
        // rotating a loop to select the other quad diagonal must not perturb
        // the chart basis through a different floating-point reduction order.
        const std::size_t n = face.Loop.size();
        if (n == 0)
            return Vec3d{};
        std::size_t start = 0;
        for (std::size_t i = 1; i < n; ++i)
            if (face.Loop[i] < face.Loop[start])
                start = i;

        // World positions are floats, so every Newell product is exact when
        // promoted to double. Accumulating there eliminates the tiny residual
        // off-axis components that otherwise make separate coplanar quads pick
        // different chart seed axes (and therefore different sample grids).
        double sumX = 0.0;
        double sumY = 0.0;
        double sumZ = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const std::size_t current = (start + i) % n;
            const std::size_t next = (start + i + 1) % n;
            const Vec3d a = transform.TransformPoint(
                mesh.Vertices[face.Loop[current]].Position);
            const Vec3d b = transform.TransformPoint(
                mesh.Vertices[face.Loop[next]].Position);
            sumX += (static_cast<double>(a.Y) - b.Y)
                * (static_cast<double>(a.Z) + b.Z);
            sumY += (static_cast<double>(a.Z) - b.Z)
                * (static_cast<double>(a.X) + b.X);
            sumZ += (static_cast<double>(a.X) - b.X)
                * (static_cast<double>(a.Y) + b.Y);
        }
        return Vec3d{ static_cast<float>(sumX), static_cast<float>(sumY),
                      static_cast<float>(sumZ) };
    }

    // Basis for a chart plane: the world axis least aligned with the normal
    // seeds U so coplanar charts with the same normal get identical axes
    // (shared luxel grid orientation). Ties break X -> Y -> Z.
    void ChartBasis(const Vec3d& normal, Vec3d& u, Vec3d& v)
    {
        const float ax = std::abs(normal.X);
        const float ay = std::abs(normal.Y);
        const float az = std::abs(normal.Z);
        Vec3d up;
        if (ax <= ay && ax <= az)
            up = Vec3d{ 1.0f, 0.0f, 0.0f };
        else if (ay <= az)
            up = Vec3d{ 0.0f, 1.0f, 0.0f };
        else
            up = Vec3d{ 0.0f, 0.0f, 1.0f };

        u = up.Cross(normal);
        if (u.SqrMagnitude() <= 1e-12f)
            u = Vec3d{ 1.0f, 0.0f, 0.0f }; // degenerate normal: any basis works
        u = u.Normalized();
        v = normal.Cross(u);
    }
} // namespace

BrushChartSet BuildBrushLightmapCharts(const BrushMesh& mesh,
                                       const Transform3f& transform,
                                       float coneDegrees,
                                       float luxelSize)
{
    BrushChartSet set;
    const std::uint32_t faceCount = static_cast<std::uint32_t>(mesh.Faces.size());
    set.FaceChart.assign(faceCount, 0u);
    if (faceCount == 0)
        return set;

    std::vector<Vec3d> newell(faceCount);
    for (std::uint32_t f = 0; f < faceCount; ++f)
        newell[f] = WorldNewell(mesh, transform, mesh.Faces[f]);

    // Undirected edge -> faces containing it as consecutive loop vertices.
    // Ordered map keys keep this reproducible; consumption is driven by the
    // stored SoftEdges order regardless.
    std::map<std::array<std::uint32_t, 2>, std::vector<std::uint32_t>> edgeFaces;
    for (std::uint32_t f = 0; f < faceCount; ++f)
    {
        const std::vector<std::uint32_t>& loop = mesh.Faces[f].Loop;
        const std::size_t n = loop.size();
        for (std::size_t i = 0; i < n; ++i)
            edgeFaces[BrushSoftEdgeKey(loop[i], loop[(i + 1) % n])].push_back(f);
    }

    // Face-level soft adjacency: the two faces meeting at each authored soft
    // edge. Non-manifold shares (more than two faces on one edge) stay hard;
    // charts only ever grow across a clean two-face seam.
    std::vector<std::vector<std::uint32_t>> softNeighbors(faceCount);
    for (const std::array<std::uint32_t, 2>& soft : mesh.SoftEdges)
    {
        const auto found = edgeFaces.find(BrushSoftEdgeKey(soft[0], soft[1]));
        if (found == edgeFaces.end() || found->second.size() != 2)
            continue;
        const std::uint32_t a = found->second[0];
        const std::uint32_t b = found->second[1];
        if (a == b)
            continue;
        softNeighbors[a].push_back(b);
        softNeighbors[b].push_back(a);
    }
    for (std::vector<std::uint32_t>& neighbors : softNeighbors)
    {
        std::sort(neighbors.begin(), neighbors.end());
        neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    }

    const float coneCosine = std::cos(coneDegrees * 3.14159265358979323846f / 180.0f);
    const float luxel = std::max(luxelSize, 1e-4f);

    // Grow charts: seed at the lowest unassigned face, BFS across soft
    // adjacency in ascending face order, admit a face while its normal stays
    // inside the cone around the chart's running area-weighted normal. A face
    // rejected for one chart seeds or joins a later one.
    std::vector<bool> assigned(faceCount, false);
    // A face counts as considered by the current seed's scan when its stamp
    // matches that seed. Versioning the marks avoids clearing a per-face bitset
    // once per chart, which is quadratic when charts approach the face count.
    // The frontier always drains before the next seed, so it is reused as is.
    std::vector<std::uint32_t> consideredStamp(faceCount, 0);
    std::vector<std::uint32_t> members;
    std::priority_queue<std::uint32_t, std::vector<std::uint32_t>, std::greater<>> frontier;
    for (std::uint32_t seed = 0; seed < faceCount; ++seed)
    {
        if (assigned[seed])
            continue;

        const std::uint32_t stamp = seed + 1;
        members.clear();
        members.push_back(seed);
        assigned[seed] = true;
        Vec3d normalSum = newell[seed];

        consideredStamp[seed] = stamp;
        for (const std::uint32_t n : softNeighbors[seed])
            if (!assigned[n] && consideredStamp[n] != stamp)
            {
                frontier.push(n);
                consideredStamp[n] = stamp;
            }

        while (!frontier.empty())
        {
            const std::uint32_t candidate = frontier.top();
            frontier.pop();
            if (assigned[candidate])
                continue;

            const Vec3d candidateNewell = newell[candidate];
            const float candidateLen = candidateNewell.Magnitude();
            const float sumLen = normalSum.Magnitude();
            bool inside = true;
            if (candidateLen > 1e-12f && sumLen > 1e-12f)
                inside = candidateNewell.Dot(normalSum) / (candidateLen * sumLen) >= coneCosine;
            if (!inside)
                continue; // stays unassigned; a later chart picks it up

            assigned[candidate] = true;
            members.push_back(candidate);
            normalSum += candidateNewell;
            for (const std::uint32_t n : softNeighbors[candidate])
                if (!assigned[n] && consideredStamp[n] != stamp)
                {
                    frontier.push(n);
                    consideredStamp[n] = stamp;
                }
        }

        BrushLightmapChart chart;
        chart.Normal = normalSum.SqrMagnitude() > 1e-12f
            ? normalSum.Normalized()
            : Vec3d{ 0.0f, 1.0f, 0.0f };
        ChartBasis(chart.Normal, chart.TangentU, chart.TangentV);

        Vec2d uvMin{ 0.0f, 0.0f };
        Vec2d uvMax{ 0.0f, 0.0f };
        bool first = true;
        for (const std::uint32_t f : members)
        {
            for (const std::uint32_t index : mesh.Faces[f].Loop)
            {
                const Vec3d world = transform.TransformPoint(mesh.Vertices[index].Position);
                const Vec2d uv{ world.Dot(chart.TangentU), world.Dot(chart.TangentV) };
                if (first)
                {
                    uvMin = uvMax = uv;
                    first = false;
                    continue;
                }
                uvMin.X = std::min(uvMin.X, uv.X);
                uvMin.Y = std::min(uvMin.Y, uv.Y);
                uvMax.X = std::max(uvMax.X, uv.X);
                uvMax.Y = std::max(uvMax.Y, uv.Y);
            }
        }
        // Floor the min to the luxel grid so coplanar same-basis charts sample
        // in phase (their shared seam texels land on the same world lattice).
        chart.UvMin = Vec2d{ std::floor(uvMin.X / luxel) * luxel,
                             std::floor(uvMin.Y / luxel) * luxel };
        chart.UvMax = uvMax;

        const std::uint32_t chartIndex = static_cast<std::uint32_t>(set.Charts.size());
        for (const std::uint32_t f : members)
            set.FaceChart[f] = chartIndex;
        set.Charts.push_back(chart);
    }

    return set;
}
