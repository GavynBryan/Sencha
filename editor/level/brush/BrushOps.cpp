#include "BrushOps.h"

#include "BrushValidation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace
{
    constexpr float kClipEps = 1e-5f;

    bool NearlyEqual(const Vec3d& a, const Vec3d& b, float tol = 1e-4f)
    {
        return (a - b).SqrMagnitude() <= tol * tol;
    }

    // Appends a face built from explicit corner positions, creating fresh vertices.
    // Coincident vertices across faces are merged later by BrushValidateAndRepair.
    // The face inherits the supplied material (the clipped piece of a source face
    // keeps its texturing; a fresh cap passes the default). (04-§1.1)
    void EmitFace(BrushMesh& mesh, const std::vector<Vec3d>& corners,
                  const FaceMaterial& material = {})
    {
        if (corners.size() < 3)
            return;
        BrushFace face;
        face.Material = material;
        face.Loop.reserve(corners.size());
        for (const Vec3d& corner : corners)
        {
            face.Loop.push_back(static_cast<std::uint32_t>(mesh.Vertices.size()));
            mesh.Vertices.push_back(BrushVertex{ corner });
        }
        mesh.Faces.push_back(std::move(face));
    }
}

BrushMesh BrushOps::MakeBox(Vec3d halfExtents)
{
    const float x = halfExtents.X;
    const float y = halfExtents.Y;
    const float z = halfExtents.Z;

    BrushMesh mesh;
    mesh.Vertices = {
        BrushVertex{ { -x, -y, -z } }, // 0
        BrushVertex{ {  x, -y, -z } }, // 1
        BrushVertex{ {  x,  y, -z } }, // 2
        BrushVertex{ { -x,  y, -z } }, // 3
        BrushVertex{ { -x, -y,  z } }, // 4
        BrushVertex{ {  x, -y,  z } }, // 5
        BrushVertex{ {  x,  y,  z } }, // 6
        BrushVertex{ { -x,  y,  z } }, // 7
    };
    // Quad faces (winding fixed up to outward by ValidateAndRepair).
    mesh.Faces = {
        BrushFace{ { 0, 1, 2, 3 }, {} }, // -Z
        BrushFace{ { 4, 5, 6, 7 }, {} }, // +Z
        BrushFace{ { 0, 1, 5, 4 }, {} }, // -Y
        BrushFace{ { 3, 2, 6, 7 }, {} }, // +Y
        BrushFace{ { 0, 3, 7, 4 }, {} }, // -X
        BrushFace{ { 1, 2, 6, 5 }, {} }, // +X
    };
    BrushValidateAndRepair(mesh);
    // Seed world-aligned UV axes from each (now-valid) face normal so a fresh box
    // textures sensibly; the material ref stays empty (= inherit level default).
    for (BrushFace& face : mesh.Faces)
        face.Material.Uv = UvProjectionForNormal(face.Normal, /*worldAligned*/ true);
    return mesh;
}

BrushMesh BrushOps::Translate(const BrushMesh& mesh, Vec3d delta)
{
    BrushMesh out = mesh;
    for (BrushVertex& vertex : out.Vertices)
        vertex.Position += delta;
    BrushValidateAndRepair(out);
    return out;
}

BrushMesh BrushOps::ResizeFace(const BrushMesh& mesh, std::uint32_t face,
                               float planePosition, float minThickness)
{
    BrushMesh out = mesh;
    if (face >= out.Faces.size())
        return out;

    const Vec3d normal = BrushComputeFaceNormal(out, out.Faces[face]);
    if (normal.SqrMagnitude() <= 0.0f)
        return out;

    // Which vertices belong to this face's loop.
    std::vector<bool> inFace(out.Vertices.size(), false);
    for (std::uint32_t index : out.Faces[face].Loop)
        inFace[index] = true;

    // Clamp so the moved face keeps minThickness against the rest of the solid.
    float maxOther = -std::numeric_limits<float>::infinity();
    for (std::size_t i = 0; i < out.Vertices.size(); ++i)
        if (!inFace[i])
            maxOther = std::max(maxOther, normal.Dot(out.Vertices[i].Position));

    float target = planePosition;
    if (std::isfinite(maxOther))
        target = std::max(target, maxOther + minThickness);

    const float current = normal.Dot(BrushFaceCentroid(out, out.Faces[face]));
    const float delta = target - current;
    for (std::uint32_t index : out.Faces[face].Loop)
        out.Vertices[index].Position += normal * delta;

    BrushValidateAndRepair(out);
    return out;
}

BrushMesh BrushOps::ExtrudeFace(const BrushMesh& mesh, std::uint32_t face, float distance)
{
    if (face >= mesh.Faces.size())
        return mesh;
    const Vec3d normal = BrushComputeFaceNormal(mesh, mesh.Faces[face]);
    if (normal.SqrMagnitude() <= 0.0f)
        return mesh;
    return ExtrudeFaceAlong(mesh, face, normal * distance);
}

BrushMesh BrushOps::ExtrudeFaceAlong(const BrushMesh& mesh, std::uint32_t face, Vec3d offset)
{
    BrushMesh out = mesh;
    if (face >= out.Faces.size())
        return out;

    const std::vector<std::uint32_t> baseLoop = out.Faces[face].Loop;
    const FaceMaterial sourceMaterial = out.Faces[face].Material; // walls keep its texture
    const std::size_t n = baseLoop.size();

    // New (extruded) ring of vertices.
    std::vector<std::uint32_t> topLoop(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        topLoop[i] = static_cast<std::uint32_t>(out.Vertices.size());
        out.Vertices.push_back(BrushVertex{ out.Vertices[baseLoop[i]].Position + offset });
    }

    // The cap moves to the extruded ring; original ring becomes the base of the
    // walls. The cap keeps its material (it translates rigidly, so its projection
    // stays pinned).
    out.Faces[face].Loop = topLoop;

    // Side wall per original edge (winding fixed up by repair). The wall keeps the
    // source texture but gets a projection for its OWN normal: inheriting the cap's
    // projection (chosen for the cap normal) would point a UV axis along the wall
    // normal and stretch the texture edge-on.
    for (std::size_t i = 0; i < n; ++i)
    {
        const std::size_t j = (i + 1) % n;
        const Vec3d edge = out.Vertices[baseLoop[j]].Position - out.Vertices[baseLoop[i]].Position;
        const Vec3d wallNormal = edge.Cross(offset); // perpendicular to edge and extrude dir

        BrushFace wall;
        wall.Loop = { baseLoop[i], baseLoop[j], topLoop[j], topLoop[i] };
        wall.Material.Material = sourceMaterial.Material;
        wall.Material.Uv = UvProjectionForNormal(wallNormal, sourceMaterial.Uv.WorldAligned);
        wall.Material.Uv.Scale = sourceMaterial.Uv.Scale; // match cap texel density
        out.Faces.push_back(std::move(wall));
    }

    BrushValidateAndRepair(out);
    return out;
}

BrushMesh BrushOps::ExtrudeEdge(const BrushMesh& mesh, std::uint32_t a, std::uint32_t b, Vec3d offset)
{
    BrushMesh out = mesh;
    if (a >= out.Vertices.size() || b >= out.Vertices.size() || a == b)
        return out;

    const Vec3d posA = out.Vertices[a].Position;
    const Vec3d posB = out.Vertices[b].Position;

    const std::uint32_t a2 = static_cast<std::uint32_t>(out.Vertices.size());
    out.Vertices.push_back(BrushVertex{ posA + offset });
    const std::uint32_t b2 = static_cast<std::uint32_t>(out.Vertices.size());
    out.Vertices.push_back(BrushVertex{ posB + offset });

    BrushFace strip;
    strip.Loop = { a, b, b2, a2 };
    strip.Material.Uv = UvProjectionForNormal((posB - posA).Cross(offset), /*worldAligned*/ true);
    out.Faces.push_back(std::move(strip));

    // Validation is the caller's (see header), so composed extrudes share base
    // indices.
    return out;
}

BrushMesh BrushOps::DeleteFace(const BrushMesh& mesh, std::uint32_t face)
{
    BrushMesh out = mesh;
    if (face >= out.Faces.size())
        return out;
    out.Faces.erase(out.Faces.begin() + face);
    BrushValidateAndRepair(out); // drops now-unreferenced vertices; flags open mesh
    return out;
}

BrushMesh BrushOps::SplitEdge(const BrushMesh& mesh, std::uint32_t a, std::uint32_t b)
{
    BrushMesh out = mesh;
    if (a >= out.Vertices.size() || b >= out.Vertices.size() || a == b)
        return out;

    const std::uint32_t mid = static_cast<std::uint32_t>(out.Vertices.size());
    out.Vertices.push_back(BrushVertex{
        (out.Vertices[a].Position + out.Vertices[b].Position) * 0.5f });

    bool inserted = false;
    for (BrushFace& face : out.Faces)
    {
        const std::size_t n = face.Loop.size();
        for (std::size_t i = 0; i < n; ++i)
        {
            const std::uint32_t u = face.Loop[i];
            const std::uint32_t v = face.Loop[(i + 1) % n];
            if ((u == a && v == b) || (u == b && v == a))
            {
                // Insert between u and v, preserving each loop's winding. An
                // undirected edge appears at most once per face loop.
                face.Loop.insert(face.Loop.begin() + static_cast<std::ptrdiff_t>(i) + 1, mid);
                inserted = true;
                break;
            }
        }
    }

    if (!inserted)
        out.Vertices.pop_back(); // edge absent: leave the mesh untouched
    // Validation is the caller's (MeshEditService) — see header.
    return out;
}

BrushMesh BrushOps::Clip(const BrushMesh& mesh, const Plane& plane, bool keepPositiveSide)
{
    const Plane p = plane.Normalized();
    auto inside = [&](const Vec3d& point) -> float
    {
        const float d = p.SignedDistanceTo(point);
        return keepPositiveSide ? d : -d; // >= 0 means "keep"
    };

    BrushMesh out;
    std::vector<std::pair<Vec3d, Vec3d>> capSegments;

    for (const BrushFace& face : mesh.Faces)
    {
        const std::size_t n = face.Loop.size();
        if (n < 3)
            continue;

        std::vector<Vec3d> clipped;
        std::vector<Vec3d> crossings;
        for (std::size_t i = 0; i < n; ++i)
        {
            const Vec3d a = mesh.Vertices[face.Loop[i]].Position;
            const Vec3d b = mesh.Vertices[face.Loop[(i + 1) % n]].Position;
            const float da = inside(a);
            const float db = inside(b);
            const bool inA = da >= -kClipEps;
            const bool inB = db >= -kClipEps;

            if (inA)
                clipped.push_back(a);
            if (inA != inB)
            {
                const float t = da / (da - db);
                const Vec3d crossing = a + (b - a) * t;
                clipped.push_back(crossing);
                crossings.push_back(crossing);
            }
        }

        if (clipped.size() >= 3)
            EmitFace(out, clipped, face.Material); // clipped piece keeps its texturing
        if (crossings.size() == 2)
            capSegments.emplace_back(crossings[0], crossings[1]);
    }

    // Chain the cut segments into the cap polygon loop.
    if (!capSegments.empty())
    {
        std::vector<Vec3d> cap;
        std::vector<bool> used(capSegments.size(), false);
        cap.push_back(capSegments[0].first);
        cap.push_back(capSegments[0].second);
        used[0] = true;

        bool extended = true;
        while (extended)
        {
            extended = false;
            for (std::size_t i = 0; i < capSegments.size(); ++i)
            {
                if (used[i])
                    continue;
                if (NearlyEqual(capSegments[i].first, cap.back()))
                {
                    cap.push_back(capSegments[i].second);
                    used[i] = true;
                    extended = true;
                }
                else if (NearlyEqual(capSegments[i].second, cap.back()))
                {
                    cap.push_back(capSegments[i].first);
                    used[i] = true;
                    extended = true;
                }
            }
        }

        // Drop the final point if it closed back onto the start.
        if (cap.size() >= 2 && NearlyEqual(cap.front(), cap.back()))
            cap.pop_back();
        if (cap.size() >= 3)
        {
            // The cut cap is a fresh face: default material, world-aligned UVs
            // from the clip plane normal (which is the cap's normal).
            FaceMaterial capMaterial;
            capMaterial.Uv = UvProjectionForNormal(p.Normal, /*worldAligned*/ true);
            EmitFace(out, cap, capMaterial);
        }
    }

    BrushValidateAndRepair(out);
    return out;
}
