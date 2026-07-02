#include "BrushValidation.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace
{
    bool NearlyCoincident(const Vec3d& a, const Vec3d& b, float tolerance)
    {
        return (a - b).SqrMagnitude() <= tolerance * tolerance;
    }

    // Removes indices equal to their predecessor (cyclically) — collapses the
    // duplicate verts a weld can leave in a loop.
    void RemoveConsecutiveDuplicates(std::vector<std::uint32_t>& loop)
    {
        if (loop.size() < 2)
            return;
        std::vector<std::uint32_t> out;
        out.reserve(loop.size());
        for (std::size_t i = 0; i < loop.size(); ++i)
        {
            const std::uint32_t current = loop[i];
            const std::uint32_t previous = out.empty() ? loop.back() : out.back();
            if (current != previous)
                out.push_back(current);
        }
        loop = std::move(out);
    }

    // Compacts vertices, dropping any not referenced by a face loop.
    bool DropUnreferenced(BrushMesh& mesh)
    {
        std::vector<bool> used(mesh.Vertices.size(), false);
        for (const BrushFace& face : mesh.Faces)
            for (std::uint32_t index : face.Loop)
                if (index < used.size())
                    used[index] = true;

        std::vector<std::uint32_t> remap(mesh.Vertices.size(), 0);
        std::vector<BrushVertex> kept;
        kept.reserve(mesh.Vertices.size());
        bool changed = false;
        for (std::size_t i = 0; i < mesh.Vertices.size(); ++i)
        {
            if (used[i])
            {
                remap[i] = static_cast<std::uint32_t>(kept.size());
                kept.push_back(mesh.Vertices[i]);
            }
            else
            {
                changed = true;
            }
        }
        if (!changed)
            return false;

        for (BrushFace& face : mesh.Faces)
            for (std::uint32_t& index : face.Loop)
                index = remap[index];
        mesh.Vertices = std::move(kept);
        return true;
    }

    // Undirected edge key (min,max) for closedness counting.
    std::uint64_t EdgeKey(std::uint32_t a, std::uint32_t b)
    {
        if (a > b)
            std::swap(a, b);
        return (static_cast<std::uint64_t>(a) << 32) | static_cast<std::uint64_t>(b);
    }
}

void BrushWeldVertices(BrushMesh& mesh, float tolerance)
{
    std::vector<std::uint32_t> remap(mesh.Vertices.size(), 0);
    std::vector<BrushVertex> merged;
    merged.reserve(mesh.Vertices.size());

    for (std::size_t i = 0; i < mesh.Vertices.size(); ++i)
    {
        const Vec3d& position = mesh.Vertices[i].Position;
        std::uint32_t target = static_cast<std::uint32_t>(merged.size());
        for (std::size_t j = 0; j < merged.size(); ++j)
        {
            if (NearlyCoincident(merged[j].Position, position, tolerance))
            {
                target = static_cast<std::uint32_t>(j);
                break;
            }
        }
        if (target == merged.size())
            merged.push_back(mesh.Vertices[i]);
        remap[i] = target;
    }

    for (BrushFace& face : mesh.Faces)
    {
        for (std::uint32_t& index : face.Loop)
            index = remap[index];
        RemoveConsecutiveDuplicates(face.Loop);
    }
    mesh.Vertices = std::move(merged);
}

BrushRepairResult BrushValidateAndRepair(BrushMesh& mesh, float weldTolerance)
{
    BrushRepairResult result;

    const BrushMesh before = mesh; // for Changed detection (small meshes)

    BrushWeldVertices(mesh, weldTolerance);
    DropUnreferenced(mesh);

    // Drop degenerate faces (<3 distinct vertices / zero-area normal).
    std::vector<BrushFace> faces;
    faces.reserve(mesh.Faces.size());
    for (BrushFace& face : mesh.Faces)
    {
        RemoveConsecutiveDuplicates(face.Loop);
        if (face.Loop.size() < 3)
        {
            result.Warnings.push_back("dropped degenerate face (<3 vertices)");
            continue;
        }
        const Vec3d normal = BrushComputeFaceNormal(mesh, face);
        if (normal.SqrMagnitude() <= 0.0f)
        {
            result.Warnings.push_back("dropped degenerate face (zero area)");
            continue;
        }
        face.Normal = normal;
        faces.push_back(std::move(face));
    }
    mesh.Faces = std::move(faces);
    DropUnreferenced(mesh);

    if (mesh.Faces.empty())
    {
        result.Warnings.push_back("brush has no valid faces");
        result.Ok = false;
        result.Changed = true;
        return result;
    }

    // Winding (and therefore normals) is left as authored: orienting outward is a
    // separate, explicit step (BrushOrientFacesOutward), so in-place edits do not
    // re-flip already-correct faces. (Construction ops and the recalc-normals verb
    // call it; this path only recomputes normals from the current winding.)

    // Closedness: every undirected edge shared by exactly two faces.
    std::unordered_map<std::uint64_t, int> edgeCounts;
    for (const BrushFace& face : mesh.Faces)
    {
        const std::size_t n = face.Loop.size();
        for (std::size_t i = 0; i < n; ++i)
            ++edgeCounts[EdgeKey(face.Loop[i], face.Loop[(i + 1) % n])];
    }
    result.Closed = true;
    for (const auto& [key, count] : edgeCounts)
    {
        (void)key;
        if (count != 2)
        {
            result.Closed = false;
            break;
        }
    }
    if (!result.Closed)
        result.Warnings.push_back("brush is not closed (open mesh)");

    result.Ok = true;

    // Changed: compare vertex/face counts and data against the snapshot.
    result.Changed = mesh.Vertices.size() != before.Vertices.size()
                  || mesh.Faces.size() != before.Faces.size();
    if (!result.Changed)
    {
        for (std::size_t i = 0; i < mesh.Faces.size() && !result.Changed; ++i)
            result.Changed = mesh.Faces[i].Loop != before.Faces[i].Loop;
    }
    return result;
}

void BrushOrientFacesOutward(BrushMesh& mesh)
{
    const std::uint32_t faceCount = static_cast<std::uint32_t>(mesh.Faces.size());
    if (faceCount == 0)
        return;

    // Undirected edge -> the directed (A,B) traversals that reference it, tagged
    // with their owning face. Two faces are consistently wound iff they traverse a
    // shared edge in opposite directions.
    struct FaceEdge { std::uint32_t Face; std::uint32_t A; std::uint32_t B; };
    std::unordered_map<std::uint64_t, std::vector<FaceEdge>> edges;
    for (std::uint32_t f = 0; f < faceCount; ++f)
    {
        const std::vector<std::uint32_t>& loop = mesh.Faces[f].Loop;
        const std::size_t n = loop.size();
        for (std::size_t i = 0; i < n; ++i)
            edges[EdgeKey(loop[i], loop[(i + 1) % n])].push_back(
                FaceEdge{ f, loop[i], loop[(i + 1) % n] });
    }

    // Flood-fill winding consistency across shared edges, per connected component.
    // Seeds and neighbours are walked in index order so the result is deterministic.
    std::vector<bool> visited(faceCount, false);
    std::vector<std::uint32_t> stack;
    for (std::uint32_t seed = 0; seed < faceCount; ++seed)
    {
        if (visited[seed])
            continue;
        visited[seed] = true;
        stack.push_back(seed);
        while (!stack.empty())
        {
            const std::uint32_t f = stack.back();
            stack.pop_back();
            const std::vector<std::uint32_t>& loop = mesh.Faces[f].Loop;
            const std::size_t n = loop.size();
            for (std::size_t i = 0; i < n; ++i)
            {
                const std::uint32_t a = loop[i];
                const std::uint32_t b = loop[(i + 1) % n];
                const auto it = edges.find(EdgeKey(a, b));
                if (it == edges.end())
                    continue;
                for (const FaceEdge& neighbor : it->second)
                {
                    if (neighbor.Face == f || visited[neighbor.Face])
                        continue;
                    // Unvisited neighbour is still in its original winding: traversing
                    // the shared edge in the SAME direction as f means it is wound the
                    // opposite way in space, so reverse it to agree.
                    if (neighbor.A == a && neighbor.B == b)
                        std::reverse(mesh.Faces[neighbor.Face].Loop.begin(),
                                     mesh.Faces[neighbor.Face].Loop.end());
                    visited[neighbor.Face] = true;
                    stack.push_back(neighbor.Face);
                }
            }
        }
    }

    for (BrushFace& face : mesh.Faces)
        face.Normal = BrushComputeFaceNormal(mesh, face);

    // The mesh is now wound consistently (all-outward or all-inward). Pick the sign.
    bool closed = true;
    for (const auto& [key, incident] : edges)
    {
        (void)key;
        if (incident.size() != 2)
        {
            closed = false;
            break;
        }
    }

    bool flipAll = false;
    if (closed)
    {
        // Signed volume via the divergence theorem: positive for outward winding.
        double volume = 0.0;
        for (const BrushFace& face : mesh.Faces)
        {
            const std::vector<std::uint32_t>& loop = face.Loop;
            const std::size_t n = loop.size();
            if (n < 3)
                continue;
            const Vec3d p0 = mesh.Vertices[loop[0]].Position;
            for (std::size_t i = 1; i + 1 < n; ++i)
            {
                const Vec3d c = mesh.Vertices[loop[i]].Position.Cross(
                    mesh.Vertices[loop[i + 1]].Position);
                volume += static_cast<double>(p0.X) * c.X
                        + static_cast<double>(p0.Y) * c.Y
                        + static_cast<double>(p0.Z) * c.Z;
            }
        }
        flipAll = volume < 0.0;
    }
    else
    {
        // Open mesh: no enclosed volume. Best-effort majority vote against the mesh
        // centroid (mutual consistency still holds; only the global sign is a guess).
        const Vec3d center = BrushMeshCentroid(mesh);
        int outward = 0;
        int inward = 0;
        for (const BrushFace& face : mesh.Faces)
        {
            if (face.Normal.Dot(BrushFaceCentroid(mesh, face) - center) >= 0.0f)
                ++outward;
            else
                ++inward;
        }
        flipAll = inward > outward;
    }

    if (flipAll)
        for (BrushFace& face : mesh.Faces)
        {
            std::reverse(face.Loop.begin(), face.Loop.end());
            face.Normal = -face.Normal;
        }
}
