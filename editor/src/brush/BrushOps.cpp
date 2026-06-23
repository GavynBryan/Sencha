#include "BrushOps.h"

#include "BrushValidation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numbers>
#include <set>
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
    BrushOrientFacesOutward(mesh); // authored winding above is inward; fix it once
    // Seed world-aligned UV axes from each (now-valid) face normal so a fresh box
    // textures sensibly; the material ref stays empty (= inherit level default).
    for (BrushFace& face : mesh.Faces)
        face.Material.Uv = UvProjectionForNormal(face.Normal, /*worldAligned*/ true);
    return mesh;
}

namespace
{
    // The two axes orthogonal to depthAxis, in ascending index order.
    std::pair<int, int> PlaneAxes(int depthAxis)
    {
        const int u = (depthAxis + 1) % 3;
        const int v = (depthAxis + 2) % 3;
        return u < v ? std::pair{ u, v } : std::pair{ v, u };
    }

    Vec3d AxisPoint(int uIdx, double u, int vIdx, double v, int dIdx, double d)
    {
        Vec3d p{};
        p[uIdx] = u;
        p[vIdx] = v;
        p[dIdx] = d;
        return p;
    }

    // World-aligned UV per face from its (now valid) normal, matching MakeBox.
    void SeedFaceUvs(BrushMesh& mesh)
    {
        for (BrushFace& face : mesh.Faces)
            face.Material.Uv = UvProjectionForNormal(face.Normal, /*worldAligned*/ true);
    }
}

BrushMesh BrushOps::MakePlane(Vec3d halfExtents, int depthAxis)
{
    const auto [uIdx, vIdx] = PlaneAxes(depthAxis);
    const double hu = halfExtents[uIdx];
    const double hv = halfExtents[vIdx];
    constexpr double d = 0.0; // flat: zero thickness on the depth axis

    BrushMesh mesh;
    EmitFace(mesh, {
        AxisPoint(uIdx, -hu, vIdx, -hv, depthAxis, d),
        AxisPoint(uIdx, hu, vIdx, -hv, depthAxis, d),
        AxisPoint(uIdx, hu, vIdx, hv, depthAxis, d),
        AxisPoint(uIdx, -hu, vIdx, hv, depthAxis, d),
    });
    BrushValidateAndRepair(mesh);

    // Repair does not reorient, so the single face keeps the winding from PlaneAxes,
    // whose normal can point along -depthAxis (down). Face it along +depthAxis,
    // matching the grid up / the surface it was placed against.
    if (!mesh.Faces.empty() && mesh.Faces[0].Normal[depthAxis] < 0.0f)
    {
        std::reverse(mesh.Faces[0].Loop.begin(), mesh.Faces[0].Loop.end());
        mesh.Faces[0].Normal = -mesh.Faces[0].Normal;
    }
    SeedFaceUvs(mesh);
    return mesh;
}

BrushMesh BrushOps::MakeCylinder(Vec3d halfExtents, int depthAxis, int sides)
{
    sides = std::max(sides, 3);
    const auto [uIdx, vIdx] = PlaneAxes(depthAxis);
    const double ru = halfExtents[uIdx];
    const double rv = halfExtents[vIdx];
    const double hd = halfExtents[depthAxis];

    std::vector<Vec3d> bottom(sides);
    std::vector<Vec3d> top(sides);
    for (int i = 0; i < sides; ++i)
    {
        const double angle = 2.0 * std::numbers::pi * i / sides;
        const double u = ru * std::cos(angle);
        const double v = rv * std::sin(angle);
        bottom[i] = AxisPoint(uIdx, u, vIdx, v, depthAxis, -hd);
        top[i] = AxisPoint(uIdx, u, vIdx, v, depthAxis, hd);
    }

    BrushMesh mesh;
    EmitFace(mesh, bottom);
    EmitFace(mesh, top);
    for (int i = 0; i < sides; ++i)
    {
        const int j = (i + 1) % sides;
        EmitFace(mesh, { bottom[i], bottom[j], top[j], top[i] });
    }
    BrushValidateAndRepair(mesh);
    BrushOrientFacesOutward(mesh); // authored caps/walls may wind inward; fix once
    SeedFaceUvs(mesh);
    return mesh;
}

BrushMesh BrushOps::MakePrimitive(BrushPrimitive kind, const BrushPrimitiveParams& params)
{
    switch (kind)
    {
        case BrushPrimitive::Plane:
            return MakePlane(params.HalfExtents, params.DepthAxis);
        case BrushPrimitive::Cylinder:
            return MakeCylinder(params.HalfExtents, params.DepthAxis, params.CylinderSides);
        case BrushPrimitive::Box:
        default:
            return MakeBox(params.HalfExtents);
    }
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

BrushMesh BrushOps::FlipFace(const BrushMesh& mesh, std::uint32_t face)
{
    BrushMesh out = mesh;
    if (face >= out.Faces.size())
        return out;
    std::reverse(out.Faces[face].Loop.begin(), out.Faces[face].Loop.end());
    out.Faces[face].Normal = -out.Faces[face].Normal;
    return out;
}

namespace
{
    // Undirected edge identity (sorted endpoints), for adjacency and midpoint maps.
    // Ordered comparison keeps every container iteration deterministic, so the
    // appended midpoint vertices land at the same indices on every run.
    struct UndirectedEdge
    {
        std::uint32_t U, V;
        UndirectedEdge(std::uint32_t a, std::uint32_t b) : U(std::min(a, b)), V(std::max(a, b)) {}
        bool operator<(const UndirectedEdge& o) const { return U != o.U ? U < o.U : V < o.V; }
    };

    // A face's edge at local index i, and the "opposite" edge of a quad (i+2).
    UndirectedEdge FaceEdge(const std::vector<std::uint32_t>& loop, std::size_t i)
    {
        return UndirectedEdge(loop[i], loop[(i + 1) % loop.size()]);
    }
}

BrushMesh BrushOps::InsertEdgeLoop(const BrushMesh& mesh, std::uint32_t a, std::uint32_t b)
{
    if (a >= mesh.Vertices.size() || b >= mesh.Vertices.size() || a == b)
        return mesh;

    // Undirected edge -> the (face, localEdgeIndex) pairs that traverse it. Built
    // straight from face loops, so it is independent of winding (no half-edge twin
    // linking to misfire after an extrude leaves local winding inconsistent) and
    // it exposes non-manifold edges (3+ incident faces) honestly instead of
    // silently dropping one side.
    std::map<UndirectedEdge, std::vector<std::pair<std::uint32_t, std::uint32_t>>> edgeFaces;
    for (std::uint32_t f = 0; f < mesh.Faces.size(); ++f)
    {
        const std::vector<std::uint32_t>& loop = mesh.Faces[f].Loop;
        for (std::size_t i = 0; i < loop.size(); ++i)
            edgeFaces[FaceEdge(loop, i)].push_back({ f, static_cast<std::uint32_t>(i) });
    }

    const UndirectedEdge seed(a, b);
    if (!edgeFaces.count(seed))
        return mesh; // seed edge not present in the mesh

    // Flood-fill the loop. A cut edge propagates to the opposite edge of every
    // adjacent quad; that adjacency is symmetric, so seeding one edge fans out in
    // BOTH directions (the single-direction half-edge walk this replaced cut only
    // half a loop and stranded midpoints on the unvisited side). Non-quads and
    // boundaries do not propagate: the loop terminates at poles and open edges.
    std::set<UndirectedEdge> cutEdges{ seed };
    std::set<std::uint32_t> splitFaces;
    std::vector<UndirectedEdge> frontier{ seed };
    while (!frontier.empty())
    {
        const UndirectedEdge e = frontier.back();
        frontier.pop_back();
        for (const auto& [f, i] : edgeFaces[e])
        {
            const std::vector<std::uint32_t>& loop = mesh.Faces[f].Loop;
            if (loop.size() != 4)
                continue; // pole: the loop ends here, this face is not split
            if (!splitFaces.insert(f).second)
                continue; // already handled
            const UndirectedEdge opposite = FaceEdge(loop, (i + 2) % 4);
            if (cutEdges.insert(opposite).second)
                frontier.push_back(opposite);
        }
    }

    if (splitFaces.empty())
        return mesh; // seed touched no quad: nothing to cut

    // Predictability gate: refuse anything that would not stay a clean 2-manifold.
    // For every cut edge, EITHER all its incident faces are being split (interior of
    // the loop, both sides cut consistently) OR it is a boundary edge (its single
    // face is split). Any other case puts a midpoint on an edge whose neighbour is
    // NOT split: a T-junction that opens the mesh. And every split quad must have
    // exactly one opposite pair cut; if the perpendicular pair is also flagged the
    // loop crosses itself in that face. In either case we return the mesh untouched,
    // so the result is always a complete loop or a no-op, never corrupt geometry.
    for (const UndirectedEdge& e : cutEdges)
        for (const auto& [f, i] : edgeFaces[e])
            if (!splitFaces.count(f))
                return mesh; // T-junction would result
    for (std::uint32_t f : splitFaces)
    {
        const std::vector<std::uint32_t>& loop = mesh.Faces[f].Loop;
        const bool pair0 = cutEdges.count(FaceEdge(loop, 0)) && cutEdges.count(FaceEdge(loop, 2));
        const bool pair1 = cutEdges.count(FaceEdge(loop, 1)) && cutEdges.count(FaceEdge(loop, 3));
        if (pair0 == pair1)
            return mesh; // neither pair, or both pairs (self-crossing): refuse
    }

    // One midpoint vertex per cut edge (deterministic order from the ordered set).
    BrushMesh out = mesh;
    std::map<UndirectedEdge, std::uint32_t> edgeMid;
    for (const UndirectedEdge& e : cutEdges)
    {
        edgeMid[e] = static_cast<std::uint32_t>(out.Vertices.size());
        out.Vertices.push_back(BrushVertex{
            (out.Vertices[e.U].Position + out.Vertices[e.V].Position) * 0.5 });
    }

    // Split each quad between the midpoints of its cut pair, preserving winding.
    std::vector<BrushFace> rebuilt;
    rebuilt.reserve(out.Faces.size() + splitFaces.size());
    for (std::uint32_t f = 0; f < out.Faces.size(); ++f)
    {
        if (!splitFaces.count(f))
        {
            rebuilt.push_back(std::move(out.Faces[f]));
            continue;
        }

        const std::vector<std::uint32_t> loop = out.Faces[f].Loop;
        // Orient the split so iCut/iCut+2 are the cut pair (guaranteed to exist by
        // the gate above). Quad [v0 v1 v2 v3] cut across edges (i0,i1) and (i2,i3):
        //   A = [mEntry, v1, v2, mExit]   B = [mExit, v3, v0, mEntry]
        const std::size_t iCut = cutEdges.count(FaceEdge(loop, 0)) ? 0 : 1;
        const std::uint32_t v0 = loop[iCut];
        const std::uint32_t v1 = loop[(iCut + 1) % 4];
        const std::uint32_t v2 = loop[(iCut + 2) % 4];
        const std::uint32_t v3 = loop[(iCut + 3) % 4];
        const std::uint32_t mEntry = edgeMid.at(UndirectedEdge(v0, v1));
        const std::uint32_t mExit = edgeMid.at(UndirectedEdge(v2, v3));

        BrushFace faceA;
        faceA.Material = out.Faces[f].Material;
        faceA.Loop = { mEntry, v1, v2, mExit };
        BrushFace faceB;
        faceB.Material = out.Faces[f].Material;
        faceB.Loop = { mExit, v3, v0, mEntry };
        rebuilt.push_back(std::move(faceA));
        rebuilt.push_back(std::move(faceB));
    }

    out.Faces = std::move(rebuilt);
    // Validation (weld + normals) is the caller's, see header.
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
    BrushOrientFacesOutward(out); // freshly rebuilt mesh: orient the cut cap + pieces
    return out;
}
