#include "BrushMesh.h"

#include <algorithm>
#include <map>
#include <cmath>

Vec3d BrushComputeFaceNormal(const BrushMesh& mesh, const BrushFace& face)
{
    // Newell's method: robust for non-planar / concave polygons, and orientation
    // follows the loop winding (CCW → outward by right-hand rule).
    const std::size_t n = face.Loop.size();
    if (n < 3)
        return Vec3d{};

    // A cyclic loop rotation is the same polygon. Start at its lowest vertex
    // index so choosing another tessellation fan cannot alter the normal via a
    // different floating-point reduction order, and accumulate products in
    // double so exactly axis-aligned faces stay exactly axis-aligned.
    std::size_t start = 0;
    for (std::size_t i = 1; i < n; ++i)
        if (face.Loop[i] < face.Loop[start])
            start = i;

    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const std::size_t current = (start + i) % n;
        const std::size_t next = (start + i + 1) % n;
        const Vec3d& a = mesh.Vertices[face.Loop[current]].Position;
        const Vec3d& b = mesh.Vertices[face.Loop[next]].Position;
        x += (static_cast<double>(a.Y) - b.Y) * (static_cast<double>(a.Z) + b.Z);
        y += (static_cast<double>(a.Z) - b.Z) * (static_cast<double>(a.X) + b.X);
        z += (static_cast<double>(a.X) - b.X) * (static_cast<double>(a.Y) + b.Y);
    }

    const Vec3d normal{ static_cast<float>(x), static_cast<float>(y),
                        static_cast<float>(z) };
    if (normal.SqrMagnitude() <= 0.0f)
        return Vec3d{ 0.0f, 0.0f, 0.0f };
    return normal.Normalized();
}

bool BrushFacesCoplanar(const BrushMesh& mesh, std::uint32_t a, std::uint32_t b)
{
    return std::abs(BrushComputeFaceNormal(mesh, mesh.Faces[a])
                        .Dot(BrushComputeFaceNormal(mesh, mesh.Faces[b])))
        > 0.99f;
}

Vec3d BrushFaceCentroid(const BrushMesh& mesh, const BrushFace& face)
{
    Vec3d sum{ 0.0f, 0.0f, 0.0f };
    if (face.Loop.empty())
        return sum;
    for (std::uint32_t index : face.Loop)
        sum += mesh.Vertices[index].Position;
    return sum * (1.0f / static_cast<float>(face.Loop.size()));
}

Vec3d BrushMeshCentroid(const BrushMesh& mesh)
{
    Vec3d sum{ 0.0f, 0.0f, 0.0f };
    if (mesh.Vertices.empty())
        return sum;
    for (const BrushVertex& vertex : mesh.Vertices)
        sum += vertex.Position;
    return sum * (1.0f / static_cast<float>(mesh.Vertices.size()));
}

Aabb3d BrushComputeBounds(const BrushMesh& mesh)
{
    Aabb3d bounds = Aabb3d::Empty();
    for (const BrushVertex& vertex : mesh.Vertices)
        bounds.ExpandToInclude(vertex.Position);
    return bounds;
}

bool BrushEdgeIsSoft(const BrushMesh& mesh, std::uint32_t a, std::uint32_t b)
{
    const std::array<std::uint32_t, 2> key = BrushSoftEdgeKey(a, b);
    return std::find(mesh.SoftEdges.begin(), mesh.SoftEdges.end(), key) != mesh.SoftEdges.end();
}

void BrushSetEdgeSoft(BrushMesh& mesh, std::uint32_t a, std::uint32_t b, bool soft)
{
    const std::array<std::uint32_t, 2> key = BrushSoftEdgeKey(a, b);
    const auto it = std::find(mesh.SoftEdges.begin(), mesh.SoftEdges.end(), key);
    if (soft && it == mesh.SoftEdges.end())
        mesh.SoftEdges.push_back(key);
    else if (!soft && it != mesh.SoftEdges.end())
        mesh.SoftEdges.erase(it);
}

void BrushSplitSoftEdge(BrushMesh& mesh, std::uint32_t a, std::uint32_t b, std::uint32_t middle)
{
    if (!BrushEdgeIsSoft(mesh, a, b))
        return;
    BrushSetEdgeSoft(mesh, a, b, false);
    BrushSetEdgeSoft(mesh, a, middle, true);
    BrushSetEdgeSoft(mesh, middle, b, true);
}

std::vector<BrushMesh> BrushConnectedComponents(const BrushMesh& mesh)
{
    // Faces sharing an undirected edge are one shell; flood over that.
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<std::size_t>> byEdge;
    for (std::size_t f = 0; f < mesh.Faces.size(); ++f)
    {
        const std::vector<std::uint32_t>& loop = mesh.Faces[f].Loop;
        for (std::size_t i = 0; i < loop.size(); ++i)
        {
            const std::uint32_t a = loop[i];
            const std::uint32_t b = loop[(i + 1) % loop.size()];
            byEdge[{ std::min(a, b), std::max(a, b) }].push_back(f);
        }
    }
    std::vector<int> shellOf(mesh.Faces.size(), -1);
    int shells = 0;
    for (std::size_t seed = 0; seed < mesh.Faces.size(); ++seed)
    {
        if (shellOf[seed] >= 0)
            continue;
        std::vector<std::size_t> stack{ seed };
        shellOf[seed] = shells;
        while (!stack.empty())
        {
            const std::size_t f = stack.back();
            stack.pop_back();
            const std::vector<std::uint32_t>& loop = mesh.Faces[f].Loop;
            for (std::size_t i = 0; i < loop.size(); ++i)
            {
                const std::uint32_t a = loop[i];
                const std::uint32_t b = loop[(i + 1) % loop.size()];
                for (std::size_t g : byEdge[{ std::min(a, b), std::max(a, b) }])
                    if (shellOf[g] < 0)
                    {
                        shellOf[g] = shells;
                        stack.push_back(g);
                    }
            }
        }
        ++shells;
    }

    std::vector<BrushMesh> out(static_cast<std::size_t>(shells));
    std::vector<std::vector<std::uint32_t>> remap(out.size(), std::vector<std::uint32_t>(mesh.Vertices.size(), 0xFFFFFFFFu));
    for (std::size_t f = 0; f < mesh.Faces.size(); ++f)
    {
        BrushMesh& shell = out[static_cast<std::size_t>(shellOf[f])];
        std::vector<std::uint32_t>& map = remap[static_cast<std::size_t>(shellOf[f])];
        BrushFace face = mesh.Faces[f];
        for (std::uint32_t& index : face.Loop)
        {
            if (map[index] == 0xFFFFFFFFu)
            {
                map[index] = static_cast<std::uint32_t>(shell.Vertices.size());
                shell.Vertices.push_back(mesh.Vertices[index]);
            }
            index = map[index];
        }
        shell.Faces.push_back(std::move(face));
    }
    // Soft edges follow their vertices.
    for (const auto& soft : mesh.SoftEdges)
        for (std::size_t s = 0; s < out.size(); ++s)
            if (remap[s][soft[0]] != 0xFFFFFFFFu && remap[s][soft[1]] != 0xFFFFFFFFu)
                out[s].SoftEdges.push_back(BrushSoftEdgeKey(remap[s][soft[0]], remap[s][soft[1]]));
    return out;
}
