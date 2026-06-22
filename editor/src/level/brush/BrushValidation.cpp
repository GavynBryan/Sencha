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

    // Orient outward: a face whose normal points toward the mesh centroid is
    // wound inward — reverse it. (Heuristic, exact for convex/star-shaped solids.)
    const Vec3d center = BrushMeshCentroid(mesh);
    for (BrushFace& face : mesh.Faces)
    {
        const Vec3d centroid = BrushFaceCentroid(mesh, face);
        if (face.Normal.Dot(centroid - center) < 0.0f)
        {
            std::reverse(face.Loop.begin(), face.Loop.end());
            face.Normal = -face.Normal;
        }
    }

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
