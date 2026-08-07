#include "BrushOps.h"

#include "BrushValidation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
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

BrushMesh BrushOps::MakePlane(Vec3d halfExtents, int depthAxis, int subdivisions)
{
    subdivisions = std::max(subdivisions, 1);
    const auto [uIdx, vIdx] = PlaneAxes(depthAxis);
    const double hu = halfExtents[uIdx];
    const double hv = halfExtents[vIdx];
    constexpr double d = 0.0; // flat: zero thickness on the depth axis

    BrushMesh mesh;
    for (int i = 0; i < subdivisions; ++i)
        for (int j = 0; j < subdivisions; ++j)
        {
            const double u0 = -hu + 2.0 * hu * i / subdivisions;
            const double u1 = -hu + 2.0 * hu * (i + 1) / subdivisions;
            const double v0 = -hv + 2.0 * hv * j / subdivisions;
            const double v1 = -hv + 2.0 * hv * (j + 1) / subdivisions;
            EmitFace(mesh, {
                AxisPoint(uIdx, u0, vIdx, v0, depthAxis, d),
                AxisPoint(uIdx, u1, vIdx, v0, depthAxis, d),
                AxisPoint(uIdx, u1, vIdx, v1, depthAxis, d),
                AxisPoint(uIdx, u0, vIdx, v1, depthAxis, d),
            });
        }
    BrushValidateAndRepair(mesh); // welds the shared grid vertices

    // Repair does not reorient, so each face keeps the winding from PlaneAxes,
    // whose normal can point along -depthAxis (down). Face them along +depthAxis,
    // matching the grid up / the surface the plane was placed against.
    for (BrushFace& face : mesh.Faces)
        if (face.Normal[depthAxis] < 0.0f)
        {
            std::reverse(face.Loop.begin(), face.Loop.end());
            face.Normal = -face.Normal;
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
            return MakePlane(params.HalfExtents, params.DepthAxis, params.PlaneSubdivisions);
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
    const FaceMaterial sourceMaterial = out.Faces[face].Material;
    const std::size_t n = baseLoop.size();

    // Each wall continues the face on the far side of its base edge: growing a
    // box sideways must extend the top's texture onto the new top strip, not
    // stamp the pulled side's texture around the extrusion. Resolve neighbors
    // against the pre-extrude topology, by value (the face array reallocates).
    struct WallSeed
    {
        FaceMaterial Material;
        Vec3d Normal;
    };
    std::vector<std::optional<WallSeed>> seeds(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const std::uint32_t a = baseLoop[i];
        const std::uint32_t b = baseLoop[(i + 1) % n];
        for (std::uint32_t f = 0; f < out.Faces.size() && !seeds[i]; ++f)
        {
            if (f == face)
                continue;
            const std::vector<std::uint32_t>& loop = out.Faces[f].Loop;
            for (std::size_t k = 0; k < loop.size(); ++k)
            {
                const std::uint32_t x = loop[k];
                const std::uint32_t y = loop[(k + 1) % loop.size()];
                if ((x == a && y == b) || (x == b && y == a))
                {
                    seeds[i] = WallSeed{ out.Faces[f].Material,
                                         BrushComputeFaceNormal(out, out.Faces[f]) };
                    break;
                }
            }
        }
    }

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

    // Side wall per original edge (winding fixed up by repair). Open-mesh edges
    // with no neighbor fall back to the pulled face's texture.
    for (std::size_t i = 0; i < n; ++i)
    {
        const std::size_t j = (i + 1) % n;
        const Vec3d edge = out.Vertices[baseLoop[j]].Position - out.Vertices[baseLoop[i]].Position;
        const Vec3d wallNormal = edge.Cross(offset); // perpendicular to edge and extrude dir

        BrushFace wall;
        wall.Loop = { baseLoop[i], baseLoop[j], topLoop[j], topLoop[i] };

        const FaceMaterial& seed = seeds[i] ? seeds[i]->Material : sourceMaterial;
        wall.Material.Material = seed.Material;

        // A wall coplanar with its neighbor continues that surface exactly, so
        // copy the projection whole (an axis-aligned extrude: the new top strip
        // IS more top). Otherwise re-derive axes for the wall's own normal
        // (inheriting axes chosen for a different plane would stretch the
        // texture edge-on), keeping the seed's alignment, density, and phase.
        const bool coplanar = seeds[i].has_value()
            && wallNormal.SqrMagnitude() > 0.0f
            && seeds[i]->Normal.SqrMagnitude() > 0.0f
            && std::abs(wallNormal.Normalized().Dot(seeds[i]->Normal.Normalized())) > 0.999f;
        if (coplanar)
        {
            wall.Material.Uv = seed.Uv;
        }
        else
        {
            wall.Material.Uv = UvProjectionForNormal(wallNormal, seed.Uv.WorldAligned);
            wall.Material.Uv.Scale = seed.Uv.Scale;
            wall.Material.Uv.Offset = seed.Uv.Offset;
            wall.Material.Uv.Rotation = seed.Uv.Rotation;
        }
        out.Faces.push_back(std::move(wall));
    }

    BrushValidateAndRepair(out);
    return out;
}

BrushMesh BrushOps::ExtrudeEdge(const BrushMesh& mesh, std::uint32_t a, std::uint32_t b, Vec3d offset,
                                const FaceMaterial* inherit)
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
    // The strip projects from its own normal; the seed face (when given) supplies
    // material, alignment, texel density, offset, and rotation so the texture
    // continues off the edge it was pulled from instead of resetting to defaults.
    const bool worldAligned = inherit == nullptr || inherit->Uv.WorldAligned;
    strip.Material.Uv = UvProjectionForNormal((posB - posA).Cross(offset), worldAligned);
    if (inherit != nullptr)
    {
        strip.Material.Material = inherit->Material;
        strip.Material.Uv.Scale = inherit->Uv.Scale;
        strip.Material.Uv.Offset = inherit->Uv.Offset;
        strip.Material.Uv.Rotation = inherit->Uv.Rotation;
    }
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

BrushMesh BrushOps::FlipAllFaces(const BrushMesh& mesh)
{
    BrushMesh out = mesh;
    for (BrushFace& face : out.Faces)
    {
        std::reverse(face.Loop.begin(), face.Loop.end());
        face.Normal = -face.Normal;
    }
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

    using EdgeFaces = std::map<UndirectedEdge, std::vector<std::pair<std::uint32_t, std::uint32_t>>>;

    // Undirected edge -> the (face, localEdgeIndex) pairs that traverse it. Built
    // straight from face loops, so it is independent of winding (no half-edge twin
    // linking to misfire after an extrude leaves local winding inconsistent) and it
    // exposes non-manifold edges (3+ incident faces) honestly instead of silently
    // dropping one side.
    EdgeFaces BuildEdgeFaces(const BrushMesh& mesh)
    {
        EdgeFaces edgeFaces;
        for (std::uint32_t f = 0; f < mesh.Faces.size(); ++f)
        {
            const std::vector<std::uint32_t>& loop = mesh.Faces[f].Loop;
            for (std::size_t i = 0; i < loop.size(); ++i)
                edgeFaces[FaceEdge(loop, i)].push_back({ f, static_cast<std::uint32_t>(i) });
        }
        return edgeFaces;
    }

    struct RingFill
    {
        std::set<UndirectedEdge> CutEdges;
        std::set<std::uint32_t> SplitFaces;
    };

    // Flood-fill the loop. A cut edge propagates to the opposite edge of every
    // adjacent quad; that adjacency is symmetric, so seeding one edge fans out in
    // BOTH directions (the single-direction half-edge walk this replaced cut only
    // half a loop and stranded midpoints on the unvisited side). Non-quads and
    // boundaries do not propagate: the loop terminates at poles and open edges.
    RingFill FloodFillRing(const BrushMesh& mesh, const EdgeFaces& edgeFaces, UndirectedEdge seed)
    {
        RingFill fill;
        fill.CutEdges.insert(seed);
        std::vector<UndirectedEdge> frontier{ seed };
        while (!frontier.empty())
        {
            const UndirectedEdge e = frontier.back();
            frontier.pop_back();
            for (const auto& [f, i] : edgeFaces.at(e))
            {
                const std::vector<std::uint32_t>& loop = mesh.Faces[f].Loop;
                if (loop.size() != 4)
                    continue; // pole: the loop ends here, this face is not split
                if (!fill.SplitFaces.insert(f).second)
                    continue; // already handled
                const UndirectedEdge opposite = FaceEdge(loop, (i + 2) % 4);
                if (fill.CutEdges.insert(opposite).second)
                    frontier.push_back(opposite);
            }
        }
        return fill;
    }
}

BrushMesh BrushOps::DissolveEdge(const BrushMesh& mesh, std::uint32_t a, std::uint32_t b)
{
    if (a >= mesh.Vertices.size() || b >= mesh.Vertices.size() || a == b)
        return mesh;

    const EdgeFaces edgeFaces = BuildEdgeFaces(mesh);
    const auto it = edgeFaces.find(UndirectedEdge(a, b));
    if (it == edgeFaces.end() || it->second.size() != 2)
        return mesh; // boundary or non-manifold edge: no well-defined pair to merge
    const auto [f1, i1] = it->second[0];
    const auto [f2, i2] = it->second[1];
    if (f1 == f2)
        return mesh; // both sides on one face (a slit): merging is meaningless

    const std::vector<std::uint32_t>& loop1 = mesh.Faces[f1].Loop;
    const std::size_t n1 = loop1.size();

    // f2's loop, rewound if needed so it traverses the shared edge opposite to
    // f1 (authoring tolerates inconsistent winding; the merge must not).
    std::vector<std::uint32_t> loop2 = mesh.Faces[f2].Loop;
    const std::size_t n2 = loop2.size();
    if (loop2[i2] == loop1[i1])
        std::reverse(loop2.begin(), loop2.end());
    std::size_t j2 = 0;
    while (j2 < n2 && !(loop2[j2] == loop1[(i1 + 1) % n1] && loop2[(j2 + 1) % n2] == loop1[i1]))
        ++j2;
    if (j2 == n2)
        return mesh;

    // All of f1's boundary starting after the shared edge, then f2's vertices
    // strictly between the shared endpoints: the union boundary minus the edge.
    std::vector<std::uint32_t> merged;
    merged.reserve(n1 + n2 - 2);
    for (std::size_t k = 0; k < n1; ++k)
        merged.push_back(loop1[(i1 + 1 + k) % n1]);
    for (std::size_t k = 2; k < n2; ++k)
        merged.push_back(loop2[(j2 + k) % n2]);

    // Faces sharing more than one edge would fold the merged loop back on
    // itself; a repeated vertex is the symptom, so refuse the merge.
    std::vector<std::uint32_t> sorted = merged;
    std::sort(sorted.begin(), sorted.end());
    if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end())
        return mesh;

    // The dissolved edge's endpoints often become straight-through boundary
    // points (two coplanar quads becoming one rectangle). Remove only those
    // redundant corners: a non-collinear endpoint still carries the silhouette
    // and must remain in the merged ngon.
    const auto removeRedundantEndpoint = [&](std::uint32_t endpoint)
    {
        const auto endpointIt = std::find(merged.begin(), merged.end(), endpoint);
        if (endpointIt == merged.end() || merged.size() <= 3)
            return;
        const std::size_t index = static_cast<std::size_t>(endpointIt - merged.begin());
        const Vec3d previous = mesh.Vertices[merged[(index + merged.size() - 1) % merged.size()]].Position;
        const Vec3d current = mesh.Vertices[endpoint].Position;
        const Vec3d next = mesh.Vertices[merged[(index + 1) % merged.size()]].Position;
        const Vec3d incoming = current - previous;
        const Vec3d outgoing = next - current;
        const double scale = incoming.Magnitude() * outgoing.Magnitude();
        if (scale <= 1e-10 || incoming.Cross(outgoing).Magnitude() > scale * 1e-5
            || incoming.Dot(outgoing) <= 0.0)
            return;
        merged.erase(endpointIt);
    };
    removeRedundantEndpoint(a);
    removeRedundantEndpoint(b);

    BrushMesh out = mesh;
    BrushFace face;
    face.Loop = std::move(merged);
    face.Material = mesh.Faces[f1].Material;
    face.Normal = BrushComputeFaceNormal(out, face);
    out.Faces.erase(out.Faces.begin() + std::max(f1, f2));
    out.Faces.erase(out.Faces.begin() + std::min(f1, f2));
    out.Faces.push_back(std::move(face));
    return out;
}

BrushMesh BrushOps::WeldVertices(const BrushMesh& mesh,
                                 std::span<const std::uint32_t> vertices, float distance)
{
    if (distance < 0.0f)
        return mesh;

    std::vector<std::uint32_t> selected;
    for (std::uint32_t v : vertices)
        if (v < mesh.Vertices.size())
            selected.push_back(v);
    std::sort(selected.begin(), selected.end());
    selected.erase(std::unique(selected.begin(), selected.end()), selected.end());
    if (selected.size() < 2)
        return mesh;

    // Union-find over the selected vertices, joining pairs within distance.
    // Sorted order keeps the roots (and thus centroids) deterministic.
    std::map<std::uint32_t, std::uint32_t> parent;
    for (std::uint32_t v : selected)
        parent[v] = v;
    const auto findRoot = [&parent](std::uint32_t v) {
        while (parent[v] != v)
            v = parent[v] = parent[parent[v]];
        return v;
    };

    const double d2 = static_cast<double>(distance) * static_cast<double>(distance);
    for (std::size_t i = 0; i < selected.size(); ++i)
        for (std::size_t j = i + 1; j < selected.size(); ++j)
        {
            const Vec3d delta = mesh.Vertices[selected[i]].Position - mesh.Vertices[selected[j]].Position;
            if (delta.SqrMagnitude() <= d2)
            {
                const std::uint32_t ri = findRoot(selected[i]);
                const std::uint32_t rj = findRoot(selected[j]);
                if (ri != rj)
                    parent[std::max(ri, rj)] = std::min(ri, rj);
            }
        }

    std::map<std::uint32_t, std::vector<std::uint32_t>> clusters;
    for (std::uint32_t v : selected)
        clusters[findRoot(v)].push_back(v);

    BrushMesh out = mesh;
    for (const auto& [root, members] : clusters)
    {
        if (members.size() < 2)
            continue;
        Vec3d centroid = {};
        for (std::uint32_t v : members)
            centroid += mesh.Vertices[v].Position;
        centroid = centroid * (1.0 / static_cast<double>(members.size()));
        for (std::uint32_t v : members)
            out.Vertices[v].Position = centroid;
    }
    return out;
}

namespace
{
    // The direction a bridge should leave a path vertex: the sum, over every
    // face bordering the incident path edges, of the in-plane direction away
    // from that face, perpendicular to the edge. Coplanar interiors cancel to
    // zero (the blend treats it as straight); a fold like a box rim yields the
    // corner bisector. `closed` wraps the last edge back to the first.
    Vec3d BoundaryTangent(const BrushMesh& mesh, const EdgeFaces& edgeFaces,
                          std::span<const std::uint32_t> path, std::size_t vertex, bool closed)
    {
        const std::size_t n = path.size();
        Vec3d sum = {};
        const auto addEdge = [&](std::size_t e)
        {
            const std::uint32_t va = path[e];
            const std::uint32_t vb = path[(e + 1) % n];
            const auto it = edgeFaces.find(UndirectedEdge(va, vb));
            if (it == edgeFaces.end())
                return;
            const Vec3d a = mesh.Vertices[va].Position;
            const Vec3d b = mesh.Vertices[vb].Position;
            const Vec3d mid = (a + b) * 0.5;
            const Vec3d edge = b - a;
            const double len2 = edge.SqrMagnitude();
            if (len2 <= 1e-12)
                return;
            for (const auto& [faceIndex, loopIndex] : it->second)
            {
                const BrushFace& face = mesh.Faces[faceIndex];
                const Vec3d interior = BrushFaceCentroid(mesh, face) - mid;
                const Vec3d perp = interior - edge * (interior.Dot(edge) / len2);
                if (perp.SqrMagnitude() > 1e-12)
                    sum += -perp.Normalized();
            }
        };

        if (vertex > 0)
            addEdge(vertex - 1);
        else if (closed)
            addEdge(n - 1);
        if (vertex < (closed ? n : n - 1))
            addEdge(vertex);
        return sum.SqrMagnitude() > 1e-12 ? sum.Normalized() : Vec3d{};
    }

    // Cubic Hermite sample between two path columns; tangents pre-scaled by the
    // chord so the bow deepens with the gap.
    Vec3d HermitePoint(Vec3d pa, Vec3d pb, Vec3d m0, Vec3d m1, double t)
    {
        const double t2 = t * t;
        const double t3 = t2 * t;
        return pa * (2.0 * t3 - 3.0 * t2 + 1.0)
             + m0 * (t3 - 2.0 * t2 + t)
             + pb * (-2.0 * t3 + 3.0 * t2)
             + m1 * (t3 - t2);
    }

    Vec3d PathCentroid(std::span<const Vec3d> points)
    {
        Vec3d sum = {};
        for (Vec3d p : points)
            sum += p;
        return sum * (1.0 / static_cast<double>(points.size()));
    }

    // Newell normal of the polygon the positions trace, unnormalized. Robust
    // for non-planar loops; the right-hand rule ties it to traversal order.
    Vec3d PathNewellNormal(std::span<const Vec3d> points)
    {
        Vec3d normal = {};
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            const Vec3d a = points[i];
            const Vec3d b = points[(i + 1) % points.size()];
            normal.X += (a.Y - b.Y) * (a.Z + b.Z);
            normal.Y += (a.Z - b.Z) * (a.X + b.X);
            normal.Z += (a.X - b.X) * (a.Y + b.Y);
        }
        return normal;
    }
}

std::vector<Vec3d> BrushOps::PathBoundaryTangents(const BrushMesh& mesh,
                                                  std::span<const std::uint32_t> path,
                                                  bool closed)
{
    std::vector<Vec3d> tangents;
    if (path.size() < 2)
        return tangents;
    const EdgeFaces edgeFaces = BuildEdgeFaces(mesh);
    tangents.reserve(path.size());
    for (std::size_t i = 0; i < path.size(); ++i)
        tangents.push_back(BoundaryTangent(mesh, edgeFaces, path, i, closed));
    return tangents;
}

BrushMesh BrushOps::BuildBridgeBetweenPaths(BridgePathSpec a, BridgePathSpec b, int segments,
                                            const FaceMaterial* inherit)
{
    const std::size_t n = a.Positions.size();
    if (n < 2 || b.Positions.size() != n || a.Closed != b.Closed || (a.Closed && n < 3))
        return {};
    if (!a.Tangents.empty() && a.Tangents.size() != n)
        return {};
    if (!b.Tangents.empty() && b.Tangents.size() != n)
        return {};
    segments = std::clamp(segments, 1, 64);

    const auto reversePath = [](BridgePathSpec& path)
    {
        std::reverse(path.Positions.begin(), path.Positions.end());
        std::reverse(path.Tangents.begin(), path.Tangents.end());
        path.WindsForward = !path.WindsForward;
    };
    const auto pairingScore = [&](const std::vector<Vec3d>& positions, std::size_t rotation)
    {
        double score = 0.0;
        for (std::size_t i = 0; i < n; ++i)
            score += (positions[(rotation + i) % n] - a.Positions[i]).SqrMagnitude();
        return score;
    };

    bool reversedQuads = false;
    if (a.Closed)
    {
        const Vec3d axis = PathCentroid(b.Positions) - PathCentroid(a.Positions);
        const Vec3d normalA = PathNewellNormal(a.Positions);
        const Vec3d normalB = PathNewellNormal(b.Positions);
        const double axisLen2 = axis.SqrMagnitude();
        const double alignA = axisLen2 > 1e-12 && normalA.SqrMagnitude() > 1e-12
            ? normalA.Normalized().Dot(axis.Normalized()) : 0.0;
        const double alignB = axisLen2 > 1e-12 && normalB.SqrMagnitude() > 1e-12
            ? normalB.Normalized().Dot(axis.Normalized()) : 0.0;
        constexpr double kAlignEpsilon = 1e-3;
        if (std::abs(alignA) > kAlignEpsilon && std::abs(alignB) > kAlignEpsilon)
        {
            // The loops' own winding against the bridge axis determines the
            // pairing direction and the quads' outward facing; distance ties
            // on symmetric loops and must only pick the rotation below.
            if ((alignB < 0.0) != (alignA < 0.0))
                reversePath(b);
            reversedQuads = alignA < 0.0;
        }
        else
        {
            // Near-coplanar loops (axis in the loop plane): no winding signal;
            // fall back to distance for the direction as well.
            BridgePathSpec reversed = b;
            reversePath(reversed);
            double bestForward = std::numeric_limits<double>::max();
            double bestBackward = std::numeric_limits<double>::max();
            for (std::size_t r = 0; r < n; ++r)
            {
                bestForward = std::min(bestForward, pairingScore(b.Positions, r));
                bestBackward = std::min(bestBackward, pairingScore(reversed.Positions, r));
            }
            if (bestBackward < bestForward)
                b = std::move(reversed);
            reversedQuads = a.WindsForward;
        }

        std::size_t bestRotation = 0;
        double bestScore = std::numeric_limits<double>::max();
        for (std::size_t r = 0; r < n; ++r)
        {
            const double score = pairingScore(b.Positions, r);
            if (score < bestScore)
            {
                bestScore = score;
                bestRotation = r;
            }
        }
        if (bestRotation != 0)
        {
            std::rotate(b.Positions.begin(),
                        b.Positions.begin() + static_cast<std::ptrdiff_t>(bestRotation),
                        b.Positions.end());
            if (!b.Tangents.empty())
                std::rotate(b.Tangents.begin(),
                            b.Tangents.begin() + static_cast<std::ptrdiff_t>(bestRotation),
                            b.Tangents.end());
        }
    }
    else
    {
        // Open runs: the endpoints anchor the pairing, so distance decides the
        // direction; the strip's facing follows a's source face winding.
        double forward = 0.0;
        double backward = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            forward += (b.Positions[i] - a.Positions[i]).SqrMagnitude();
            backward += (b.Positions[n - 1 - i] - a.Positions[i]).SqrMagnitude();
        }
        if (backward < forward)
            reversePath(b);
        reversedQuads = a.WindsForward;
    }

    BrushMesh out;
    out.Vertices.reserve(2 * n + static_cast<std::size_t>(segments - 1) * n);
    for (Vec3d p : a.Positions)
        out.Vertices.push_back(BrushVertex{ p });
    for (Vec3d p : b.Positions)
        out.Vertices.push_back(BrushVertex{ p });

    std::vector<std::vector<std::uint32_t>> rows(static_cast<std::size_t>(segments) + 1);
    for (std::size_t i = 0; i < n; ++i)
    {
        rows.front().push_back(static_cast<std::uint32_t>(i));
        rows.back().push_back(static_cast<std::uint32_t>(n + i));
    }
    for (std::size_t i = 0; i < n; ++i)
    {
        const Vec3d pa = a.Positions[i];
        const Vec3d pb = b.Positions[i];
        const double chord = (pb - pa).Magnitude();
        const Vec3d m0 = (a.Tangents.empty() ? Vec3d{} : a.Tangents[i]) * chord;
        const Vec3d m1 = (b.Tangents.empty() ? Vec3d{} : b.Tangents[i]) * -chord;
        for (int r = 1; r < segments; ++r)
        {
            const double t = static_cast<double>(r) / segments;
            rows[static_cast<std::size_t>(r)].push_back(static_cast<std::uint32_t>(out.Vertices.size()));
            out.Vertices.push_back(BrushVertex{ HermitePoint(pa, pb, m0, m1, t) });
        }
    }

    const std::size_t columns = a.Closed ? n : n - 1;
    for (int r = 0; r < segments; ++r)
        for (std::size_t i = 0; i < columns; ++i)
        {
            const std::size_t next = (i + 1) % n;
            const std::vector<std::uint32_t>& rowA = rows[static_cast<std::size_t>(r)];
            const std::vector<std::uint32_t>& rowB = rows[static_cast<std::size_t>(r) + 1];
            BrushFace face;
            face.Loop = reversedQuads
                ? std::vector<std::uint32_t>{ rowA[next], rowA[i], rowB[i], rowB[next] }
                : std::vector<std::uint32_t>{ rowA[i], rowA[next], rowB[next], rowB[i] };
            if (inherit != nullptr)
                face.Material = *inherit;
            face.Normal = BrushComputeFaceNormal(out, face);
            out.Faces.push_back(std::move(face));
        }
    return out;
}

BrushMesh BrushOps::BridgeEdgePaths(const BrushMesh& mesh,
                                    std::span<const std::uint32_t> pathA,
                                    std::span<const std::uint32_t> pathB,
                                    int segments,
                                    const FaceMaterial* inherit,
                                    bool closed)
{
    const std::size_t n = pathA.size();
    if (n < 2 || pathB.size() != n)
        return mesh;
    std::set<std::uint32_t> unique;
    for (std::uint32_t v : pathA)
        if (v >= mesh.Vertices.size() || !unique.insert(v).second)
            return mesh;
    for (std::uint32_t v : pathB)
        if (v >= mesh.Vertices.size() || !unique.insert(v).second)
            return mesh;
    segments = std::clamp(segments, 1, 64);

    const EdgeFaces edgeFaces = BuildEdgeFaces(mesh);

    // Wind quads to continue pathA's bordering face across the shared edge
    // (opposite traversal), matching ExtrudeEdge's manifold-consistent flap.
    bool reversed = false;
    if (const auto it = edgeFaces.find(UndirectedEdge(pathA[0], pathA[1])); it != edgeFaces.end())
        for (const auto& [f, i] : it->second)
        {
            const std::vector<std::uint32_t>& loop = mesh.Faces[f].Loop;
            if (loop[i] == pathA[0] && loop[(i + 1) % loop.size()] == pathA[1])
                reversed = true;
        }

    BrushMesh out = mesh;

    // Row 0 is pathA, row `segments` is pathB; the rows between are appended,
    // one Hermite sample per column. Tangents scale with the chord so the bow
    // deepens with the gap, and vanish (straight lerp) on interior columns.
    std::vector<std::vector<std::uint32_t>> rows(static_cast<std::size_t>(segments) + 1);
    rows.front().assign(pathA.begin(), pathA.end());
    rows.back().assign(pathB.begin(), pathB.end());
    for (std::size_t i = 0; i < n; ++i)
    {
        const Vec3d pa = mesh.Vertices[pathA[i]].Position;
        const Vec3d pb = mesh.Vertices[pathB[i]].Position;
        const double chord = (pb - pa).Magnitude();
        const Vec3d m0 = BoundaryTangent(mesh, edgeFaces, pathA, i, closed) * chord;
        const Vec3d m1 = BoundaryTangent(mesh, edgeFaces, pathB, i, closed) * -chord;
        for (int r = 1; r < segments; ++r)
        {
            const double t = static_cast<double>(r) / segments;
            rows[static_cast<std::size_t>(r)].push_back(static_cast<std::uint32_t>(out.Vertices.size()));
            out.Vertices.push_back(BrushVertex{ HermitePoint(pa, pb, m0, m1, t) });
        }
    }

    const std::size_t columns = closed ? n : n - 1;
    for (int r = 0; r < segments; ++r)
        for (std::size_t i = 0; i < columns; ++i)
        {
            const std::size_t next = (i + 1) % n;
            const std::vector<std::uint32_t>& rowA = rows[static_cast<std::size_t>(r)];
            const std::vector<std::uint32_t>& rowB = rows[static_cast<std::size_t>(r) + 1];
            BrushFace face;
            face.Loop = reversed
                ? std::vector<std::uint32_t>{ rowA[next], rowA[i], rowB[i], rowB[next] }
                : std::vector<std::uint32_t>{ rowA[i], rowA[next], rowB[next], rowB[i] };
            if (inherit != nullptr)
                face.Material = *inherit;
            face.Normal = BrushComputeFaceNormal(out, face);
            out.Faces.push_back(std::move(face));
        }
    return out;
}

BrushOps::BrushEdgeRing BrushOps::TraceEdgeRing(const BrushMesh& mesh, std::uint32_t a, std::uint32_t b)
{
    if (a >= mesh.Vertices.size() || b >= mesh.Vertices.size() || a == b)
        return {};

    const EdgeFaces edgeFaces = BuildEdgeFaces(mesh);
    const UndirectedEdge seed(a, b);
    if (!edgeFaces.count(seed))
        return {}; // seed edge not present in the mesh

    const RingFill fill = FloodFillRing(mesh, edgeFaces, seed);

    BrushEdgeRing ring;
    ring.StripFaces.assign(fill.SplitFaces.begin(), fill.SplitFaces.end());
    ring.RingEdges.reserve(fill.CutEdges.size());
    for (const UndirectedEdge& e : fill.CutEdges)
        ring.RingEdges.push_back({ e.U, e.V });
    return ring;
}

std::vector<std::array<std::uint32_t, 2>> BrushOps::TraceEdgeLoop(const BrushMesh& mesh,
                                                                  std::uint32_t a, std::uint32_t b)
{
    if (a >= mesh.Vertices.size() || b >= mesh.Vertices.size() || a == b)
        return {};

    const EdgeFaces edgeFaces = BuildEdgeFaces(mesh);
    const UndirectedEdge seed(a, b);
    if (!edgeFaces.count(seed))
        return {}; // seed edge not present in the mesh

    // Edges incident to each vertex, for valence checks and stepping. Same source
    // (face loops) as edgeFaces, so it shares its winding-independence.
    std::map<std::uint32_t, std::set<UndirectedEdge>> vertexEdges;
    for (const auto& [edge, faces] : edgeFaces)
    {
        vertexEdges[edge.U].insert(edge);
        vertexEdges[edge.V].insert(edge);
    }

    auto faceSet = [&](const UndirectedEdge& e) {
        std::set<std::uint32_t> faces;
        for (const auto& [f, i] : edgeFaces.at(e))
            faces.insert(f);
        return faces;
    };
    auto otherEnd = [](const UndirectedEdge& e, std::uint32_t v) { return e.U == v ? e.V : e.U; };

    // The loop continuation of edge e past vertex v.
    auto stepAcross = [&](const UndirectedEdge& e, std::uint32_t v) -> std::optional<UndirectedEdge> {
        const std::set<UndirectedEdge>& incident = vertexEdges.at(v);
        if (incident.size() == 4)
        {
            // Regular interior vertex: the unique edge whose faces are disjoint from
            // e's (it goes "straight on" rather than turning along a shared face).
            // Purely topological, so it is exact under any quad winding.
            const std::set<std::uint32_t> eFaces = faceSet(e);
            std::optional<UndirectedEdge> next;
            for (const UndirectedEdge& cand : incident)
            {
                if (cand.U == e.U && cand.V == e.V)
                    continue;
                const std::set<std::uint32_t> candFaces = faceSet(cand);
                bool disjoint = true;
                for (std::uint32_t f : candFaces)
                    if (eFaces.count(f)) { disjoint = false; break; }
                if (!disjoint)
                    continue;
                if (next.has_value())
                    return std::nullopt; // ambiguous: not a clean loop, stop here
                next = cand;
            }
            return next;
        }

        // Irregular vertex (cap rim, open boundary, pole). A 3-face fan is topologically
        // identical whether it is a loop continuation or a dead end, so fall back to
        // geometry: continue to the straightest edge, and only if the turn stays under
        // 90 degrees (a sharp corner ends the loop). This carries an edge loop around a
        // cylinder cap rim or an open boundary; it stops at box corners (90 degree turns)
        // and reversals.
        const Vec3d travel = mesh.Vertices[v].Position - mesh.Vertices[otherEnd(e, v)].Position;
        if (travel.SqrMagnitude() <= 0.0f)
            return std::nullopt;
        const Vec3d dir = travel.Normalized();

        std::optional<UndirectedEdge> next;
        double bestDot = 0.0; // strictly straighter than a right angle
        for (const UndirectedEdge& cand : incident)
        {
            if (cand.U == e.U && cand.V == e.V)
                continue;
            const Vec3d step = mesh.Vertices[otherEnd(cand, v)].Position - mesh.Vertices[v].Position;
            if (step.SqrMagnitude() <= 0.0f)
                continue;
            const double dot = dir.Dot(step.Normalized());
            if (dot > bestDot)
            {
                bestDot = dot;
                next = cand;
            }
        }
        return next;
    };

    std::set<UndirectedEdge> visited{ seed };
    // Walk both endpoints of the seed outward until a pole, boundary, or cycle.
    for (std::uint32_t start : { seed.V, seed.U })
    {
        UndirectedEdge current = seed;
        std::uint32_t vertex = start;
        while (true)
        {
            const std::optional<UndirectedEdge> next = stepAcross(current, vertex);
            if (!next.has_value())
                break;
            if (!visited.insert(*next).second)
                break; // cycled back into the loop
            vertex = (next->U == vertex) ? next->V : next->U;
            current = *next;
        }
    }

    std::vector<std::array<std::uint32_t, 2>> loop;
    loop.reserve(visited.size());
    for (const UndirectedEdge& e : visited)
        loop.push_back({ e.U, e.V });
    return loop;
}

BrushMesh BrushOps::InsertEdgeLoop(const BrushMesh& mesh, std::uint32_t a, std::uint32_t b, float position)
{
    if (a >= mesh.Vertices.size() || b >= mesh.Vertices.size() || a == b)
        return mesh;

    EdgeFaces edgeFaces = BuildEdgeFaces(mesh);
    const UndirectedEdge seed(a, b);
    if (!edgeFaces.count(seed))
        return mesh; // seed edge not present in the mesh

    const RingFill fill = FloodFillRing(mesh, edgeFaces, seed);
    const std::set<UndirectedEdge>& cutEdges = fill.CutEdges;
    const std::set<std::uint32_t>& splitFaces = fill.SplitFaces;

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

    // Orient every cut edge consistently around the loop so the cut sits at the same
    // parametric position on each. Propagate a "t=0 endpoint" from the seed (t=0 at
    // a) across each split quad to its opposite edge: the endpoints joined by a quad
    // side share the t=0 side. (At t=0.5 the orientation is moot — it is the midpoint.)
    const float t = position < 0.02f ? 0.02f : (position > 0.98f ? 0.98f : position);
    std::map<UndirectedEdge, std::uint32_t> forward;
    forward[seed] = a;
    {
        std::vector<UndirectedEdge> frontier{ seed };
        std::set<std::uint32_t> propagated;
        while (!frontier.empty())
        {
            const UndirectedEdge e = frontier.back();
            frontier.pop_back();
            for (const auto& [f, i] : edgeFaces.at(e))
            {
                if (!splitFaces.count(f) || !propagated.insert(f).second)
                    continue;
                const std::vector<std::uint32_t>& loop = mesh.Faces[f].Loop;
                const UndirectedEdge opposite = FaceEdge(loop, (i + 2) % 4);
                const std::uint32_t fwd = forward.at(e);
                // The t=0 endpoint of e (loop[i] or loop[i+1]) joins, via a quad side,
                // loop[i+3] or loop[i+2] respectively: that is opposite's t=0 endpoint.
                const std::uint32_t fwdOpposite =
                    (fwd == loop[i]) ? loop[(i + 3) % 4] : loop[(i + 2) % 4];
                if (forward.emplace(opposite, fwdOpposite).second)
                    frontier.push_back(opposite);
            }
        }
    }

    // One split vertex per cut edge, at position t from its t=0 endpoint.
    BrushMesh out = mesh;
    std::map<UndirectedEdge, std::uint32_t> edgeMid;
    for (const UndirectedEdge& e : cutEdges)
    {
        const std::uint32_t fwd = forward.count(e) ? forward.at(e) : e.U;
        const std::uint32_t other = (e.U == fwd) ? e.V : e.U;
        edgeMid[e] = static_cast<std::uint32_t>(out.Vertices.size());
        out.Vertices.push_back(BrushVertex{
            out.Vertices[fwd].Position * (1.0f - t) + out.Vertices[other].Position * t });
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

BrushMesh BrushOps::InsertEdgeCut(const BrushMesh& mesh, std::uint32_t a, std::uint32_t b,
                                 float position, std::uint32_t faceIndex)
{
    if (a >= mesh.Vertices.size() || b >= mesh.Vertices.size() || a == b)
        return mesh;

    const EdgeFaces edgeFaces = BuildEdgeFaces(mesh);
    const auto seedIt = edgeFaces.find(UndirectedEdge(a, b));
    if (seedIt == edgeFaces.end())
        return mesh;

    const float t = position < 0.02f ? 0.02f : (position > 0.98f ? 0.98f : position);
    BrushMesh out = mesh;

    // One shared split vertex on the seed edge (t from a toward b).
    const std::uint32_t mSeed = static_cast<std::uint32_t>(out.Vertices.size());
    out.Vertices.push_back(BrushVertex{
        out.Vertices[a].Position * (1.0f - t) + out.Vertices[b].Position * t });

    std::set<std::uint32_t> splitFaces;
    std::vector<BrushFace> halves;
    // Each split point also belongs in every uncut neighbour that borders its
    // host edge. Keeping those points in the neighbour loop makes it an ngon
    // rather than leaving a T-junction across an unchanged quad.
    std::map<UndirectedEdge, std::vector<std::uint32_t>> edgePoints;
    edgePoints[UndirectedEdge(a, b)].push_back(mSeed);
    for (const auto& [f, i] : seedIt->second)
    {
        if (faceIndex != kAllAdjacentFaces && f != faceIndex)
            continue; // restricted to the one face under the cursor
        const std::vector<std::uint32_t>& loop = mesh.Faces[f].Loop;
        if (loop.size() != 4)
            continue; // only quads cut cleanly
        splitFaces.insert(f);

        // Quad [v0 v1 v2 v3] with the seed at edge (v0, v1). Split it across mSeed (on
        // v0-v1) and a new mOpp on the opposite edge (v2-v3), at the matching t: the
        // opposite endpoint joined to a by a quad side is its t=0 end.
        const std::uint32_t v0 = loop[i];
        const std::uint32_t v1 = loop[(i + 1) % 4];
        const std::uint32_t v2 = loop[(i + 2) % 4];
        const std::uint32_t v3 = loop[(i + 3) % 4];
        const std::uint32_t fwdOpp = (v0 == a) ? v3 : v2; // v0->v3 side, v1->v2 side
        const std::uint32_t othOpp = (fwdOpp == v2) ? v3 : v2;
        const std::uint32_t mOpp = static_cast<std::uint32_t>(out.Vertices.size());
        out.Vertices.push_back(BrushVertex{
            out.Vertices[fwdOpp].Position * (1.0f - t) + out.Vertices[othOpp].Position * t });
        edgePoints[UndirectedEdge(v2, v3)].push_back(mOpp);

        BrushFace faceA;
        faceA.Material = mesh.Faces[f].Material;
        faceA.Loop = { mSeed, v1, v2, mOpp };
        BrushFace faceB;
        faceB.Material = mesh.Faces[f].Material;
        faceB.Loop = { mOpp, v3, v0, mSeed };
        halves.push_back(std::move(faceA));
        halves.push_back(std::move(faceB));
    }

    if (splitFaces.empty())
        return mesh; // seed touched no quad

    std::vector<BrushFace> rebuilt;
    rebuilt.reserve(out.Faces.size() + halves.size());
    for (std::uint32_t f = 0; f < out.Faces.size(); ++f)
    {
        if (splitFaces.count(f))
            continue;

        BrushFace neighbour = std::move(out.Faces[f]);
        std::vector<std::uint32_t> loop;
        loop.reserve(neighbour.Loop.size() + 2);
        for (std::size_t i = 0; i < neighbour.Loop.size(); ++i)
        {
            const std::uint32_t from = neighbour.Loop[i];
            const std::uint32_t to = neighbour.Loop[(i + 1) % neighbour.Loop.size()];
            loop.push_back(from);
            const auto pointsIt = edgePoints.find(UndirectedEdge(from, to));
            if (pointsIt == edgePoints.end())
                continue;

            std::vector<std::pair<double, std::uint32_t>> ordered;
            const Vec3d start = mesh.Vertices[from].Position;
            const Vec3d edge = mesh.Vertices[to].Position - start;
            const double length2 = edge.SqrMagnitude();
            for (std::uint32_t point : pointsIt->second)
            {
                const double parameter = length2 > 0.0
                    ? (out.Vertices[point].Position - start).Dot(edge) / length2 : 0.0;
                ordered.emplace_back(parameter, point);
            }
            std::sort(ordered.begin(), ordered.end());
            for (const auto& [unused, point] : ordered)
                loop.push_back(point);
        }
        neighbour.Loop = std::move(loop);
        rebuilt.push_back(std::move(neighbour));
    }
    for (BrushFace& half : halves)
        rebuilt.push_back(std::move(half));
    out.Faces = std::move(rebuilt);
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

namespace
{
    // Carve tolerances. Shape checks are relative (dimensionless); snap is twice
    // the weld tolerance so a snapped-but-not-flush gap can never weld into a
    // sliver (BrushWeldVertices compares inclusively at 1e-4).
    constexpr float kCarveShapeTol = 1e-3f;
    constexpr float kCarveSnapTol = 2e-4f;
}

std::optional<BrushOps::BrushRectFaceFrame> BrushOps::RectFaceFrame(const BrushMesh& mesh,
                                                                    std::uint32_t face)
{
    if (face >= mesh.Faces.size() || mesh.Faces[face].Loop.size() != 4)
        return std::nullopt;

    const std::vector<std::uint32_t>& loop = mesh.Faces[face].Loop;
    const Vec3d p0 = mesh.Vertices[loop[0]].Position;
    const Vec3d p1 = mesh.Vertices[loop[1]].Position;
    const Vec3d p2 = mesh.Vertices[loop[2]].Position;
    const Vec3d p3 = mesh.Vertices[loop[3]].Position;

    const Vec3d e0 = p1 - p0;
    const Vec3d e2 = p3 - p2;
    const Vec3d d = p3 - p0;
    const float width = e0.Magnitude();
    const float height = d.Magnitude();
    if (width < 4.0f * kCarveSnapTol || height < 4.0f * kCarveSnapTol)
        return std::nullopt;

    // Parallelogram (opposite edges cancel), which also implies planarity; then
    // a right angle between the two frame axes makes it a rectangle.
    if ((e0 + e2).Magnitude() > kCarveShapeTol * std::max(width, height))
        return std::nullopt;
    const Vec3d u = e0 * (1.0f / width);
    const Vec3d v = d * (1.0f / height);
    if (std::abs(u.Dot(v)) > kCarveShapeTol)
        return std::nullopt;

    return BrushRectFaceFrame{ .Origin = p0, .AxisU = u, .AxisV = v, .Width = width, .Height = height };
}

namespace
{
    struct FrameLoopSeed
    {
        std::uint32_t A = 0;
        std::uint32_t B = 0;
        float Position = 0.5f;
    };

    std::optional<FrameLoopSeed> FindFrameBoundarySeed(
        const BrushMesh& mesh,
        const BrushOps::BrushRectFaceFrame& frame,
        bool cutAlongU,
        float bound)
    {
        const Vec3d normal = frame.AxisU.Cross(frame.AxisV);
        if (normal.SqrMagnitude() <= 0.0f)
            return std::nullopt;

        const EdgeFaces edgeFaces = BuildEdgeFaces(mesh);
        std::optional<FrameLoopSeed> best;
        float bestSpan = std::numeric_limits<float>::max();
        for (const auto& entry : edgeFaces)
        {
            const UndirectedEdge& edge = entry.first;
            const Vec3d pa = mesh.Vertices[edge.U].Position;
            const Vec3d pb = mesh.Vertices[edge.V].Position;
            const Vec3d ra = pa - frame.Origin;
            const Vec3d rb = pb - frame.Origin;
            if (std::abs(ra.Dot(normal)) > kCarveSnapTol || std::abs(rb.Dot(normal)) > kCarveSnapTol)
                continue;

            const Vec2d a{ ra.Dot(frame.AxisU), ra.Dot(frame.AxisV) };
            const Vec2d b{ rb.Dot(frame.AxisU), rb.Dot(frame.AxisV) };
            if (cutAlongU)
            {
                if (std::abs(a.Y) > kCarveSnapTol || std::abs(b.Y) > kCarveSnapTol)
                    continue;
            }
            else
            {
                if (std::abs(a.X) > kCarveSnapTol || std::abs(b.X) > kCarveSnapTol)
                    continue;
            }

            const float av = cutAlongU ? a.X : a.Y;
            const float bv = cutAlongU ? b.X : b.Y;
            const float lo = std::min(av, bv);
            const float hi = std::max(av, bv);
            if (hi - lo <= kCarveSnapTol)
                continue;
            if (bound <= lo + kCarveSnapTol || bound >= hi - kCarveSnapTol)
                continue;

            const float span = hi - lo;
            if (span >= bestSpan)
                continue;

            best = FrameLoopSeed{
                .A = edge.U,
                .B = edge.V,
                .Position = std::clamp((bound - av) / (bv - av), 0.0f, 1.0f),
            };
            bestSpan = span;
        }
        return best;
    }

    std::vector<float> InteriorFrameBounds(float a, float b, float limit)
    {
        float lo = std::clamp(std::min(a, b), 0.0f, limit);
        float hi = std::clamp(std::max(a, b), 0.0f, limit);
        const auto snap = [limit](float x)
        {
            if (x <= kCarveSnapTol)
                return 0.0f;
            if (x >= limit - kCarveSnapTol)
                return limit;
            return x;
        };
        lo = snap(lo);
        hi = snap(hi);
        if (hi - lo <= kCarveSnapTol)
            return {};

        std::vector<float> bounds;
        if (lo > kCarveSnapTol && lo < limit - kCarveSnapTol)
            bounds.push_back(lo);
        if (hi > kCarveSnapTol && hi < limit - kCarveSnapTol
            && (bounds.empty() || std::abs(hi - bounds.back()) > kCarveSnapTol))
            bounds.push_back(hi);
        return bounds;
    }
}

BrushMesh BrushOps::InsertFaceLoopBounds(const BrushMesh& mesh, std::uint32_t face,
                                         Vec2d rectMin, Vec2d rectMax)
{
    const std::optional<BrushRectFaceFrame> frame = RectFaceFrame(mesh, face);
    if (!frame.has_value())
        return mesh;

    const std::vector<float> uBounds = InteriorFrameBounds(rectMin.X, rectMax.X, frame->Width);
    const std::vector<float> vBounds = InteriorFrameBounds(rectMin.Y, rectMax.Y, frame->Height);
    if (uBounds.empty() && vBounds.empty())
        return mesh;

    BrushMesh out = mesh;

    // True when an edge already runs along the bound line in the face plane:
    // a rect side flush with an existing loop needs no new cut there.
    const auto boundHasExistingEdge = [&](bool cutAlongU, float bound) -> bool
    {
        const Vec3d normal = frame->AxisU.Cross(frame->AxisV);
        for (const BrushFace& f : out.Faces)
        {
            const std::vector<std::uint32_t>& loop = f.Loop;
            for (std::size_t i = 0; i < loop.size(); ++i)
            {
                const Vec3d ra = out.Vertices[loop[i]].Position - frame->Origin;
                const Vec3d rb = out.Vertices[loop[(i + 1) % loop.size()]].Position - frame->Origin;
                if (std::abs(ra.Dot(normal)) > kCarveSnapTol || std::abs(rb.Dot(normal)) > kCarveSnapTol)
                    continue;
                const float coordA = cutAlongU ? ra.Dot(frame->AxisU) : ra.Dot(frame->AxisV);
                const float coordB = cutAlongU ? rb.Dot(frame->AxisU) : rb.Dot(frame->AxisV);
                const float lateralA = cutAlongU ? ra.Dot(frame->AxisV) : ra.Dot(frame->AxisU);
                const float lateralB = cutAlongU ? rb.Dot(frame->AxisV) : rb.Dot(frame->AxisU);
                if (std::abs(coordA - bound) <= kCarveSnapTol && std::abs(coordB - bound) <= kCarveSnapTol
                    && std::abs(lateralA - lateralB) > kCarveSnapTol)
                    return true;
            }
        }
        return false;
    };

    const auto applyBound = [&](bool cutAlongU, float bound) -> bool
    {
        const std::optional<FrameLoopSeed> seed = FindFrameBoundarySeed(out, *frame, cutAlongU, bound);
        if (!seed.has_value())
            return boundHasExistingEdge(cutAlongU, bound);

        const std::size_t beforeVertices = out.Vertices.size();
        const std::size_t beforeFaces = out.Faces.size();
        out = InsertEdgeLoop(out, seed->A, seed->B, seed->Position);
        return out.Vertices.size() > beforeVertices && out.Faces.size() > beforeFaces;
    };

    for (float u : uBounds)
        if (!applyBound(/*cutAlongU*/ true, u))
            return mesh;
    for (float v : vBounds)
        if (!applyBound(/*cutAlongU*/ false, v))
            return mesh;

    return out;
}

BrushMesh BrushOps::CarveFaceRect(const BrushMesh& mesh, std::uint32_t face,
                                  Vec2d rectMin, Vec2d rectMax)
{
    const std::optional<BrushRectFaceFrame> frame = RectFaceFrame(mesh, face);
    if (!frame.has_value())
        return mesh;
    const float width = frame->Width;
    const float height = frame->Height;

    // Canonicalize: order, clamp to the face, snap flush within tolerance.
    float u0 = std::clamp(std::min(rectMin.X, rectMax.X), 0.0f, width);
    float u1 = std::clamp(std::max(rectMin.X, rectMax.X), 0.0f, width);
    float v0 = std::clamp(std::min(rectMin.Y, rectMax.Y), 0.0f, height);
    float v1 = std::clamp(std::max(rectMin.Y, rectMax.Y), 0.0f, height);
    const auto snap = [](float& x, float limit)
    {
        if (x <= kCarveSnapTol)
            x = 0.0f;
        if (x >= limit - kCarveSnapTol)
            x = limit;
    };
    snap(u0, width);
    snap(u1, width);
    snap(v0, height);
    snap(v1, height);

    if (u1 - u0 <= kCarveSnapTol || v1 - v0 <= kCarveSnapTol)
        return mesh; // zero-size or sliver rect (covers a rect entirely off the face too)

    // Flush flags per host side, CCW from the bottom edge h0-h1.
    const bool flush[4] = { v0 == 0.0f, u1 == width, v1 == height, u0 == 0.0f };
    if (flush[0] && flush[1] && flush[2] && flush[3])
        return mesh; // rect covers the face: no-op per spec

    BrushMesh out = mesh;
    const std::array<std::uint32_t, 4> host = {
        mesh.Faces[face].Loop[0], mesh.Faces[face].Loop[1],
        mesh.Faces[face].Loop[2], mesh.Faces[face].Loop[3],
    };

    // Rect corners S0..S3, CCW matching the host corners. A corner flush in BOTH
    // coordinates reuses the host corner index (never mint a duplicate there: it
    // would weld into a degenerate repeated-index loop).
    const Vec2d cornerUv[4] = { { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } };
    const bool atHostCorner[4] = {
        flush[3] && flush[0], // S0 at h0: left + bottom
        flush[1] && flush[0], // S1 at h1: right + bottom
        flush[1] && flush[2], // S2 at h2: right + top
        flush[3] && flush[2], // S3 at h3: left + top
    };
    std::array<std::uint32_t, 4> rectIdx{};
    for (int k = 0; k < 4; ++k)
    {
        if (atHostCorner[k])
        {
            rectIdx[k] = host[static_cast<std::size_t>(k)];
            continue;
        }
        rectIdx[k] = static_cast<std::uint32_t>(out.Vertices.size());
        out.Vertices.push_back(BrushVertex{
            frame->Origin + frame->AxisU * cornerUv[k].X + frame->AxisV * cornerUv[k].Y });
    }

    // Shared split vertices for the flush sides: every OTHER face bordering the
    // flush host edge gains the newly minted corner(s) in its loop, ordered by
    // parameter along that face's own traversal of the edge, so the mesh stays
    // closed and the neighbor loop stays simple regardless of its winding. The
    // neighbor's current loop is rescanned per side because an earlier side may
    // already have inserted into the same face.
    const EdgeFaces edgeFaces = BuildEdgeFaces(mesh);
    for (int k = 0; k < 4; ++k)
    {
        if (!flush[k])
            continue;
        const std::uint32_t a = host[static_cast<std::size_t>(k)];
        const std::uint32_t b = host[static_cast<std::size_t>((k + 1) % 4)];

        // (index, parameter along the DIRECTED host edge a -> b) per minted point.
        std::vector<std::pair<std::uint32_t, float>> points;
        const auto paramOf = [&](int corner)
        {
            switch (k)
            {
            case 0: return cornerUv[corner].X / width;             // bottom: by u
            case 1: return cornerUv[corner].Y / height;            // right: by v
            case 2: return (width - cornerUv[corner].X) / width;   // top: by W-u
            default: return (height - cornerUv[corner].Y) / height; // left: by H-v
            }
        };
        if (!atHostCorner[k])
            points.emplace_back(rectIdx[static_cast<std::size_t>(k)], paramOf(k));
        if (!atHostCorner[(k + 1) % 4])
            points.emplace_back(rectIdx[static_cast<std::size_t>((k + 1) % 4)], paramOf((k + 1) % 4));
        if (points.empty())
            continue;

        const auto it = edgeFaces.find(UndirectedEdge(a, b));
        if (it == edgeFaces.end())
            continue;
        for (const auto& [neighborFace, unusedEdgeIndex] : it->second)
        {
            if (neighborFace == face)
                continue;
            std::vector<std::uint32_t>& loop = out.Faces[neighborFace].Loop;
            for (std::size_t j = 0; j < loop.size(); ++j)
            {
                const std::uint32_t x = loop[j];
                const std::uint32_t y = loop[(j + 1) % loop.size()];
                if (!((x == a && y == b) || (x == b && y == a)))
                    continue;
                std::vector<std::pair<std::uint32_t, float>> ordered = points;
                std::sort(ordered.begin(), ordered.end(),
                          [ascending = (x == a)](const auto& l, const auto& r)
                          { return ascending ? l.second < r.second : l.second > r.second; });
                for (std::size_t p = 0; p < ordered.size(); ++p)
                    loop.insert(loop.begin() + static_cast<std::ptrdiff_t>(j + 1 + p),
                                ordered[p].first);
                break;
            }
        }
    }

    // Rebuild: the host face is replaced by the ring quads (one per non-flush
    // side) and the center rectangle, appended LAST. Every loop is CCW in the
    // frame, so with AxisU x AxisV = host normal no winding repair is needed
    // (and none exists: repair recomputes normals but never re-winds).
    const FaceMaterial material = mesh.Faces[face].Material;
    const Vec3d normal = mesh.Faces[face].Normal;
    out.Faces.erase(out.Faces.begin() + face);

    const auto appendFace = [&](std::array<std::uint32_t, 4> loop)
    {
        BrushFace piece;
        piece.Loop.assign(loop.begin(), loop.end());
        piece.Material = material;
        piece.Normal = normal;
        out.Faces.push_back(std::move(piece));
    };
    for (int k = 0; k < 4; ++k)
        if (!flush[k])
            appendFace({ host[static_cast<std::size_t>(k)], host[static_cast<std::size_t>((k + 1) % 4)],
                         rectIdx[static_cast<std::size_t>((k + 1) % 4)], rectIdx[static_cast<std::size_t>(k)] });
    appendFace({ rectIdx[0], rectIdx[1], rectIdx[2], rectIdx[3] });

    return out;
}

namespace
{
    std::array<Vec2d, 4> RectUvCorners(Vec2d rectMin, Vec2d rectMax)
    {
        return {{
            { rectMin.X, rectMin.Y },
            { rectMax.X, rectMin.Y },
            { rectMax.X, rectMax.Y },
            { rectMin.X, rectMax.Y },
        }};
    }

    std::array<Vec3d, 4> RectFrameCorners(const BrushOps::BrushRectFaceFrame& frame,
                                          const std::array<Vec2d, 4>& corners)
    {
        std::array<Vec3d, 4> out{};
        for (std::size_t i = 0; i < corners.size(); ++i)
            out[i] = frame.Origin + frame.AxisU * corners[i].X + frame.AxisV * corners[i].Y;
        return out;
    }

    bool StrictlyInsideFrame(Vec2d uv, const BrushOps::BrushRectFaceFrame& frame)
    {
        return uv.X > kCarveSnapTol && uv.X < frame.Width - kCarveSnapTol
            && uv.Y > kCarveSnapTol && uv.Y < frame.Height - kCarveSnapTol;
    }

    struct ThroughFaceCandidate
    {
        std::uint32_t Face = 0;
        Vec2d RectMin = {};
        Vec2d RectMax = {};
        std::array<Vec3d, 4> Projected = {};
        float Distance = std::numeric_limits<float>::max();
    };

    bool ProjectedCornersFormFrameRect(const std::array<Vec2d, 4>& uv,
                                       Vec2d rectMin, Vec2d rectMax)
    {
        constexpr float kRectCornerTol = 1e-3f;
        const std::array<Vec2d, 4> expected = RectUvCorners(rectMin, rectMax);
        for (Vec2d p : uv)
        {
            bool matched = false;
            for (Vec2d q : expected)
                matched |= (p - q).SqrMagnitude() <= kRectCornerTol * kRectCornerTol;
            if (!matched)
                return false;
        }
        return true;
    }

    std::optional<ThroughFaceCandidate> FindThroughFace(
        const BrushMesh& mesh,
        std::uint32_t sourceFace,
        Vec3d sourceNormal,
        const std::array<Vec3d, 4>& sourceCorners,
        bool allowFlush = false)
    {
        const Vec3d direction = -sourceNormal;
        std::optional<ThroughFaceCandidate> best;
        for (std::uint32_t f = 0; f < mesh.Faces.size(); ++f)
        {
            if (f == sourceFace)
                continue;
            const std::optional<BrushOps::BrushRectFaceFrame> targetFrame =
                BrushOps::RectFaceFrame(mesh, f);
            if (!targetFrame.has_value())
                continue;

            const Vec3d targetNormal = BrushComputeFaceNormal(mesh, mesh.Faces[f]);
            if (targetNormal.SqrMagnitude() <= 0.0f || sourceNormal.Dot(targetNormal) > -0.98f)
                continue;

            const float denom = direction.Dot(targetNormal);
            if (std::abs(denom) <= 1e-5f)
                continue;

            std::array<Vec2d, 4> targetUv{};
            std::array<Vec3d, 4> projected{};
            bool contained = true;
            float distanceSum = 0.0f;
            for (std::size_t i = 0; i < sourceCorners.size(); ++i)
            {
                const float distance =
                    (targetFrame->Origin - sourceCorners[i]).Dot(targetNormal) / denom;
                if (distance <= kCarveSnapTol)
                {
                    contained = false;
                    break;
                }
                projected[i] = sourceCorners[i] + direction * distance;
                const Vec3d rel = projected[i] - targetFrame->Origin;
                targetUv[i] = { rel.Dot(targetFrame->AxisU), rel.Dot(targetFrame->AxisV) };
                const bool withinTarget = allowFlush
                    ? targetUv[i].X >= -kCarveSnapTol && targetUv[i].X <= targetFrame->Width + kCarveSnapTol
                        && targetUv[i].Y >= -kCarveSnapTol && targetUv[i].Y <= targetFrame->Height + kCarveSnapTol
                    : StrictlyInsideFrame(targetUv[i], *targetFrame);
                if (!withinTarget)
                {
                    contained = false;
                    break;
                }
                distanceSum += distance;
            }
            if (!contained)
                continue;

            Vec2d rectMin = targetUv[0];
            Vec2d rectMax = targetUv[0];
            for (Vec2d uv : targetUv)
            {
                rectMin.X = std::min(rectMin.X, uv.X);
                rectMin.Y = std::min(rectMin.Y, uv.Y);
                rectMax.X = std::max(rectMax.X, uv.X);
                rectMax.Y = std::max(rectMax.Y, uv.Y);
            }
            if (!ProjectedCornersFormFrameRect(targetUv, rectMin, rectMax))
                continue;

            const float averageDistance = distanceSum / static_cast<float>(sourceCorners.size());
            if (!best.has_value() || averageDistance < best->Distance)
                best = ThroughFaceCandidate{
                    .Face = f,
                    .RectMin = rectMin,
                    .RectMax = rectMax,
                    .Projected = projected,
                    .Distance = averageDistance,
                };
        }
        return best;
    }

    // The quad face lying in `frame`'s plane whose vertices are exactly the
    // rect corners (matched in frame UV space, tolerating the interpolation
    // drift of loop-minted vertices). This is how the through-loop path finds
    // the cap faces the wrapping cuts produced.
    std::optional<std::uint32_t> FindRectFaceInFrame(const BrushMesh& mesh,
                                                     const BrushOps::BrushRectFaceFrame& frame,
                                                     Vec2d rectMin, Vec2d rectMax)
    {
        const Vec3d normal = frame.AxisU.Cross(frame.AxisV);
        const std::array<Vec2d, 4> expected = RectUvCorners(rectMin, rectMax);
        for (std::uint32_t f = 0; f < mesh.Faces.size(); ++f)
        {
            const std::vector<std::uint32_t>& loop = mesh.Faces[f].Loop;
            if (loop.size() != 4)
                continue;
            std::array<bool, 4> used{};
            bool matches = true;
            for (std::uint32_t v : loop)
            {
                const Vec3d rel = mesh.Vertices[v].Position - frame.Origin;
                if (std::abs(rel.Dot(normal)) > kCarveShapeTol)
                {
                    matches = false;
                    break;
                }
                const Vec2d uv{ rel.Dot(frame.AxisU), rel.Dot(frame.AxisV) };
                bool found = false;
                for (int k = 0; k < 4 && !found; ++k)
                    if (!used[k] && (uv - expected[k]).SqrMagnitude() <= kCarveShapeTol * kCarveShapeTol)
                    {
                        used[k] = true;
                        found = true;
                    }
                if (!found)
                {
                    matches = false;
                    break;
                }
            }
            if (matches)
                return f;
        }
        return std::nullopt;
    }

    // Removes the source and target rect cap faces and bridges their rims into
    // tunnel walls. `projected[k]` is the target-side position under source
    // rect corner k (RectUvCorners order); the source cap's own loop order is
    // arbitrary (a loop-minted cap starts anywhere), so each rim vertex is
    // identified by its rect corner in `sourceFrame` UV space first. Walls wind
    // to continue the source cap's rim traversal, which keeps the tunnel
    // interior consistent. `skipWallSide[k]` (rect side k in CarveFaceRect's
    // CCW convention) omits that wall: a flush pierce opens there instead of
    // walling. nullopt when either cap is not the expected quad or a rim
    // vertex cannot be matched.
    std::optional<BrushMesh> BridgeCapsIntoTunnel(BrushMesh out,
                                                  std::uint32_t sourceCap,
                                                  std::uint32_t targetCap,
                                                  const BrushOps::BrushRectFaceFrame& sourceFrame,
                                                  Vec2d sourceMin, Vec2d sourceMax,
                                                  const std::array<Vec3d, 4>& projected,
                                                  const FaceMaterial& material,
                                                  const std::array<bool, 4>& skipWallSide = {})
    {
        const std::vector<std::uint32_t> sourceLoop = out.Faces[sourceCap].Loop;
        const std::vector<std::uint32_t> targetLoop = out.Faces[targetCap].Loop;
        if (sourceLoop.size() != 4 || targetLoop.size() != 4)
            return std::nullopt;

        const std::array<Vec2d, 4> cornerUv = RectUvCorners(sourceMin, sourceMax);
        std::array<int, 4> cornerOf{};
        std::array<bool, 4> used{};
        for (std::size_t i = 0; i < 4; ++i)
        {
            const Vec3d rel = out.Vertices[sourceLoop[i]].Position - sourceFrame.Origin;
            const Vec2d uv{ rel.Dot(sourceFrame.AxisU), rel.Dot(sourceFrame.AxisV) };
            int match = -1;
            for (int k = 0; k < 4 && match < 0; ++k)
                if (!used[k] && (uv - cornerUv[k]).SqrMagnitude() <= kCarveShapeTol * kCarveShapeTol)
                    match = k;
            if (match < 0)
                return std::nullopt;
            used[match] = true;
            cornerOf[i] = match;
        }

        std::array<std::uint32_t, 4> targetAtCorner{};
        targetAtCorner.fill(std::numeric_limits<std::uint32_t>::max());
        for (int k = 0; k < 4; ++k)
            for (std::uint32_t v : targetLoop)
                if ((out.Vertices[v].Position - projected[static_cast<std::size_t>(k)]).SqrMagnitude()
                    <= kCarveShapeTol * kCarveShapeTol)
                {
                    targetAtCorner[static_cast<std::size_t>(k)] = v;
                    break;
                }
        for (std::uint32_t v : targetAtCorner)
            if (v == std::numeric_limits<std::uint32_t>::max())
                return std::nullopt;

        const auto eraseFace = [&](std::uint32_t index)
        {
            out.Faces.erase(out.Faces.begin() + static_cast<std::ptrdiff_t>(index));
        };
        if (sourceCap > targetCap)
        {
            eraseFace(sourceCap);
            eraseFace(targetCap);
        }
        else
        {
            eraseFace(targetCap);
            eraseFace(sourceCap);
        }

        for (std::size_t i = 0; i < 4; ++i)
        {
            const std::size_t j = (i + 1) % 4;
            // The rect side this rim edge covers; the cap loop may traverse
            // the rect corners in either direction.
            int side;
            if (cornerOf[j] == (cornerOf[i] + 1) % 4)
                side = cornerOf[i];
            else if (cornerOf[i] == (cornerOf[j] + 1) % 4)
                side = cornerOf[j];
            else
                return std::nullopt; // rim edge spans non-adjacent rect corners
            if (skipWallSide[static_cast<std::size_t>(side)])
                continue;
            BrushFace wall;
            wall.Loop = {
                sourceLoop[i],
                sourceLoop[j],
                targetAtCorner[static_cast<std::size_t>(cornerOf[j])],
                targetAtCorner[static_cast<std::size_t>(cornerOf[i])],
            };
            wall.Material = material;
            wall.Normal = BrushComputeFaceNormal(out, wall);
            // The cap's projection axes lie in the cap plane; a tunnel wall is
            // perpendicular to it, so inheriting them verbatim stretches the
            // texture edge-on. Re-derive axes for the wall's own normal, keeping
            // the cap's scale/offset/rotation (same rule as ExtrudeFaceAlong).
            wall.Material.Uv = UvProjectionForNormal(wall.Normal, material.Uv.WorldAligned);
            wall.Material.Uv.Scale = material.Uv.Scale;
            wall.Material.Uv.Offset = material.Uv.Offset;
            wall.Material.Uv.Rotation = material.Uv.Rotation;
            out.Faces.push_back(std::move(wall));
        }
        return out;
    }

    // Match 4 vertex indices to a frame rect's corner slots (RectUvCorners
    // order) by their UV position. nullopt unless the match is a bijection.
    std::optional<std::array<std::uint32_t, 4>> MatchRectCornerSlots(
        const BrushMesh& mesh,
        const BrushOps::BrushRectFaceFrame& frame,
        Vec2d rectMin, Vec2d rectMax,
        std::span<const std::uint32_t> candidates)
    {
        const std::array<Vec2d, 4> expected = RectUvCorners(rectMin, rectMax);
        std::array<std::uint32_t, 4> slots{};
        slots.fill(std::numeric_limits<std::uint32_t>::max());
        for (std::uint32_t v : candidates)
        {
            const Vec3d rel = mesh.Vertices[v].Position - frame.Origin;
            const Vec2d uv{ rel.Dot(frame.AxisU), rel.Dot(frame.AxisV) };
            for (int k = 0; k < 4; ++k)
                if ((uv - expected[static_cast<std::size_t>(k)]).SqrMagnitude()
                        <= kCarveShapeTol * kCarveShapeTol
                    && slots[static_cast<std::size_t>(k)] == std::numeric_limits<std::uint32_t>::max())
                {
                    slots[static_cast<std::size_t>(k)] = v;
                    break;
                }
        }
        for (std::uint32_t v : slots)
            if (v == std::numeric_limits<std::uint32_t>::max())
                return std::nullopt;
        return slots;
    }

    // Flush flags of a rect against its frame, per side in CarveFaceRect's CCW
    // convention (bottom, right, top, left).
    std::array<bool, 4> FrameFlushSides(const BrushOps::BrushRectFaceFrame& frame,
                                        Vec2d rectMin, Vec2d rectMax)
    {
        return {
            rectMin.Y <= kCarveSnapTol,
            rectMax.X >= frame.Width - kCarveSnapTol,
            rectMax.Y >= frame.Height - kCarveSnapTol,
            rectMin.X <= kCarveSnapTol,
        };
    }

    bool PointOnSegment(Vec3d p, Vec3d a, Vec3d b)
    {
        const Vec3d dir = b - a;
        const double len2 = dir.SqrMagnitude();
        if (len2 <= 1e-12)
            return false;
        const Vec3d rel = p - a;
        const double t = rel.Dot(dir) / len2;
        if (t < -1e-4 || t > 1.0 + 1e-4)
            return false;
        return (rel - dir * t).SqrMagnitude() <= kCarveShapeTol * kCarveShapeTol;
    }

    // The face across the host edge segment containing both points. Used to
    // classify a flush side: a coplanar neighbor means the surface continues
    // (the side seams against an interior edge and needs a wall); a bent
    // neighbor means the brush boundary turns and the side opens a notch.
    std::optional<std::uint32_t> NeighborAcrossSegment(const BrushMesh& mesh,
                                                       const EdgeFaces& edgeFaces,
                                                       std::uint32_t excludeFace,
                                                       std::span<const std::uint32_t> hostLoop,
                                                       Vec3d p0, Vec3d p1)
    {
        for (std::size_t e = 0; e < hostLoop.size(); ++e)
        {
            const std::uint32_t a = hostLoop[e];
            const std::uint32_t b = hostLoop[(e + 1) % hostLoop.size()];
            if (!PointOnSegment(p0, mesh.Vertices[a].Position, mesh.Vertices[b].Position)
                || !PointOnSegment(p1, mesh.Vertices[a].Position, mesh.Vertices[b].Position))
                continue;
            const auto it = edgeFaces.find(UndirectedEdge(a, b));
            if (it == edgeFaces.end())
                return std::nullopt;
            for (const auto& [f, unused] : it->second)
                if (f != excludeFace)
                    return f;
        }
        return std::nullopt;
    }

    // Inserts existing referenced vertices that lie on a face's edges as
    // collinear loop vertices, so a freshly built wall cannot leave
    // T-junctions against the subdivided geometry a channel consumed.
    void AbsorbCollinearVertices(BrushMesh& mesh, std::uint32_t face)
    {
        std::vector<bool> referenced(mesh.Vertices.size(), false);
        for (const BrushFace& f : mesh.Faces)
            for (std::uint32_t v : f.Loop)
                if (v < referenced.size())
                    referenced[v] = true;

        std::vector<std::uint32_t>& loop = mesh.Faces[face].Loop;
        for (std::size_t i = 0; i < loop.size();)
        {
            const Vec3d a = mesh.Vertices[loop[i]].Position;
            const Vec3d b = mesh.Vertices[loop[(i + 1) % loop.size()]].Position;
            const Vec3d dir = b - a;
            const double len2 = dir.SqrMagnitude();
            if (len2 <= 1e-12)
            {
                ++i;
                continue;
            }
            std::vector<std::pair<float, std::uint32_t>> hits;
            for (std::uint32_t v = 0; v < mesh.Vertices.size(); ++v)
            {
                if (!referenced[v]
                    || std::find(loop.begin(), loop.end(), v) != loop.end())
                    continue;
                const Vec3d rel = mesh.Vertices[v].Position - a;
                const double t = rel.Dot(dir) / len2;
                if (t <= 1e-4 || t >= 1.0 - 1e-4)
                    continue;
                if ((rel - dir * t).SqrMagnitude() > kCarveShapeTol * kCarveShapeTol)
                    continue;
                hits.emplace_back(static_cast<float>(t), v);
            }
            if (hits.empty())
            {
                ++i;
                continue;
            }
            std::sort(hits.begin(), hits.end());
            for (std::size_t h = 0; h < hits.size(); ++h)
                loop.insert(loop.begin() + static_cast<std::ptrdiff_t>(i + 1 + h), hits[h].second);
            i += hits.size() + 1;
        }
    }

    // One-pass retopology for a through-carve whose rect is flush with one or
    // more face sides: opens the source and target caps, consumes everything
    // the channel crosses on each flush side's plane (faces fully inside are
    // deleted, faces partially covered are ring-cut around their overlap), and
    // walls the non-flush sides. One pass because composing CarveFaceRect
    // calls is circular: carving either cap inserts split vertices into the
    // shared neighbor, destroying the rectangular loop the other cap's carve
    // needs. Scope: every face the channel crosses on a flush plane must be a
    // flat rectangular quad axis-aligned with the notch, and together they
    // must tile the notch; opposite-pair-only flush (which would split the
    // brush in two) and full cover are the caller's rejects. Returns nullopt
    // to mean "leave the mesh unchanged".
    std::optional<BrushMesh> CarveThroughWithFlushSides(
        const BrushMesh& mesh,
        std::uint32_t face,
        const BrushOps::BrushRectFaceFrame& sourceFrame,
        Vec2d sourceMin, Vec2d sourceMax,
        const std::array<bool, 4>& flush,
        const ThroughFaceCandidate& target,
        const BrushOps::BrushRectFaceFrame& targetFrame)
    {
        if (mesh.Faces[face].Loop.size() != 4 || mesh.Faces[target.Face].Loop.size() != 4)
            return std::nullopt;

        // Flush must agree side-for-side: a wall coplanar with a neighbor face
        // (or a notch missing one side of its cut) is not a valid result.
        const std::array<bool, 4> targetFlush = FrameFlushSides(targetFrame, target.RectMin, target.RectMax);
        const int sourceFlushCount = static_cast<int>(flush[0]) + static_cast<int>(flush[1])
                                   + static_cast<int>(flush[2]) + static_cast<int>(flush[3]);
        const int targetFlushCount = static_cast<int>(targetFlush[0]) + static_cast<int>(targetFlush[1])
                                   + static_cast<int>(targetFlush[2]) + static_cast<int>(targetFlush[3]);
        if (sourceFlushCount != targetFlushCount)
            return std::nullopt;

        const std::array<std::uint32_t, 4> host = {
            mesh.Faces[face].Loop[0], mesh.Faces[face].Loop[1],
            mesh.Faces[face].Loop[2], mesh.Faces[face].Loop[3],
        };
        const std::array<std::uint32_t, 4> targetHost = {
            mesh.Faces[target.Face].Loop[0], mesh.Faces[target.Face].Loop[1],
            mesh.Faces[target.Face].Loop[2], mesh.Faces[target.Face].Loop[3],
        };

        BrushMesh out = mesh;

        // Source rect corners S0..S3 (RectUvCorners order): reuse the host
        // corner when flush in both coordinates, mint otherwise.
        const std::array<Vec2d, 4> cornerUv = RectUvCorners(sourceMin, sourceMax);
        const bool atHostCorner[4] = {
            flush[3] && flush[0],
            flush[1] && flush[0],
            flush[1] && flush[2],
            flush[3] && flush[2],
        };
        std::array<std::uint32_t, 4> rectIdx{};
        for (int k = 0; k < 4; ++k)
        {
            if (atHostCorner[k])
            {
                rectIdx[static_cast<std::size_t>(k)] = host[static_cast<std::size_t>(k)];
                continue;
            }
            rectIdx[static_cast<std::size_t>(k)] = static_cast<std::uint32_t>(out.Vertices.size());
            out.Vertices.push_back(BrushVertex{ sourceFrame.Origin
                + sourceFrame.AxisU * cornerUv[static_cast<std::size_t>(k)].X
                + sourceFrame.AxisV * cornerUv[static_cast<std::size_t>(k)].Y });
        }

        // Target-side corners, indexed by SOURCE corner k (target.Projected
        // order): reuse a target host vertex when the projection lands on one.
        std::array<std::uint32_t, 4> targetIdx{};
        for (int k = 0; k < 4; ++k)
        {
            const Vec3d p = target.Projected[static_cast<std::size_t>(k)];
            std::uint32_t found = std::numeric_limits<std::uint32_t>::max();
            for (std::uint32_t v : targetHost)
                if ((out.Vertices[v].Position - p).SqrMagnitude() <= kCarveShapeTol * kCarveShapeTol)
                {
                    found = v;
                    break;
                }
            if (found == std::numeric_limits<std::uint32_t>::max())
            {
                found = static_cast<std::uint32_t>(out.Vertices.size());
                out.Vertices.push_back(BrushVertex{ p });
            }
            targetIdx[static_cast<std::size_t>(k)] = found;
        }

        // Consume everything the channel crosses on each flush side's plane.
        // Planned entirely before any face is touched, so every reject leaves
        // the input unchanged.
        const auto mintOrReuse = [&](Vec3d position) -> std::uint32_t
        {
            for (std::uint32_t v = 0; v < out.Vertices.size(); ++v)
                if ((out.Vertices[v].Position - position).SqrMagnitude()
                    <= kCarveShapeTol * kCarveShapeTol)
                    return v;
            out.Vertices.push_back(BrushVertex{ position });
            return static_cast<std::uint32_t>(out.Vertices.size() - 1);
        };

        struct NeighborCut
        {
            std::uint32_t Face = 0;
            std::array<std::uint32_t, 4> Host{};
            std::array<std::uint32_t, 4> Rect{}; // overlap corners in the face frame's slot order
            std::array<bool, 4> Flush{};
            bool FullyCovered = false;
        };
        std::vector<NeighborCut> neighborCuts;
        std::array<bool, 4> seamSide{};
        std::set<std::uint32_t> claimed;
        const EdgeFaces edgeFaces = BuildEdgeFaces(mesh);
        const Vec3d sourceNormal = BrushComputeFaceNormal(mesh, mesh.Faces[face]);
        const Vec3d targetNormal = BrushComputeFaceNormal(mesh, mesh.Faces[target.Face]);
        for (int k = 0; k < 4; ++k)
        {
            if (!flush[k])
                continue;

            const Vec3d sourceP0 = out.Vertices[rectIdx[static_cast<std::size_t>(k)]].Position;
            const Vec3d sourceP1 = out.Vertices[rectIdx[static_cast<std::size_t>((k + 1) % 4)]].Position;
            const Vec3d targetP0 = out.Vertices[targetIdx[static_cast<std::size_t>(k)]].Position;
            const Vec3d targetP1 = out.Vertices[targetIdx[static_cast<std::size_t>((k + 1) % 4)]].Position;

            const std::optional<std::uint32_t> sourceNeighbor =
                NeighborAcrossSegment(mesh, edgeFaces, face, host, sourceP0, sourceP1);
            const std::optional<std::uint32_t> targetNeighbor =
                NeighborAcrossSegment(mesh, edgeFaces, target.Face, targetHost, targetP0, targetP1);
            if (!sourceNeighbor.has_value() || !targetNeighbor.has_value())
                return std::nullopt;

            // Seam: the surface continues past the flush edge (a coplanar
            // neighbor, e.g. a rect side on an interior loop cut). The channel
            // gets a wall there and the on-edge corners join the neighbor's
            // loop; only a bent boundary opens a notch. Half-seam channels
            // (seam on one cap, boundary on the other) need a real volume
            // boolean: refused.
            const bool sourceSeam = std::abs(
                BrushComputeFaceNormal(mesh, mesh.Faces[*sourceNeighbor]).Dot(sourceNormal)) > 0.99f;
            const bool targetSeam = std::abs(
                BrushComputeFaceNormal(mesh, mesh.Faces[*targetNeighbor]).Dot(targetNormal)) > 0.99f;
            if (sourceSeam != targetSeam)
                return std::nullopt;
            if (sourceSeam)
            {
                seamSide[static_cast<std::size_t>(k)] = true;
                continue;
            }

            if (*sourceNeighbor == target.Face)
                return std::nullopt;
            const std::optional<BrushOps::BrushRectFaceFrame> planeFrame =
                BrushOps::RectFaceFrame(mesh, *sourceNeighbor);
            if (!planeFrame.has_value())
                return std::nullopt;
            const Vec3d planeNormal = planeFrame->AxisU.Cross(planeFrame->AxisV);

            // This side's channel: the flush rect corners and their target
            // counterparts, all of which must lie in the neighbor plane.
            const std::array<Vec3d, 4> notchCorners = { sourceP0, sourceP1, targetP0, targetP1 };
            for (const Vec3d& corner : notchCorners)
                if (std::abs((corner - planeFrame->Origin).Dot(planeNormal)) > kCarveShapeTol)
                    return std::nullopt; // channel leaves the side plane: not box-like here
            const double notchArea =
                std::sqrt((notchCorners[1] - notchCorners[0]).SqrMagnitude())
                * std::sqrt((notchCorners[2] - notchCorners[0]).SqrMagnitude());

            // Every face on this plane whose rectangle the channel crosses is
            // consumed: deleted when fully covered, ring-cut around the
            // overlap otherwise. Together they must tile the whole channel.
            double coveredArea = 0.0;
            for (std::uint32_t f = 0; f < mesh.Faces.size(); ++f)
            {
                if (f == face || f == target.Face || claimed.count(f) != 0)
                    continue;
                bool onPlane = true;
                for (std::uint32_t v : mesh.Faces[f].Loop)
                    if (std::abs((mesh.Vertices[v].Position - planeFrame->Origin).Dot(planeNormal))
                        > kCarveShapeTol)
                    {
                        onPlane = false;
                        break;
                    }
                if (!onPlane)
                    continue;

                const std::optional<BrushOps::BrushRectFaceFrame> faceFrame =
                    BrushOps::RectFaceFrame(mesh, f);
                Vec2d notchMin{ std::numeric_limits<float>::max(),
                                std::numeric_limits<float>::max() };
                Vec2d notchMax{ std::numeric_limits<float>::lowest(),
                                std::numeric_limits<float>::lowest() };
                if (faceFrame.has_value())
                {
                    bool axisAligned = true;
                    for (const Vec3d& corner : notchCorners)
                    {
                        const Vec3d rel = corner - faceFrame->Origin;
                        const Vec2d uv{ rel.Dot(faceFrame->AxisU), rel.Dot(faceFrame->AxisV) };
                        notchMin.X = std::min(notchMin.X, uv.X);
                        notchMin.Y = std::min(notchMin.Y, uv.Y);
                        notchMax.X = std::max(notchMax.X, uv.X);
                        notchMax.Y = std::max(notchMax.Y, uv.Y);
                    }
                    for (const Vec3d& corner : notchCorners)
                    {
                        const Vec3d rel = corner - faceFrame->Origin;
                        const Vec2d uv{ rel.Dot(faceFrame->AxisU), rel.Dot(faceFrame->AxisV) };
                        const bool onU = std::abs(uv.X - notchMin.X) <= kCarveShapeTol
                                      || std::abs(uv.X - notchMax.X) <= kCarveShapeTol;
                        const bool onV = std::abs(uv.Y - notchMin.Y) <= kCarveShapeTol
                                      || std::abs(uv.Y - notchMax.Y) <= kCarveShapeTol;
                        axisAligned &= onU && onV;
                    }
                    if (!axisAligned)
                        return std::nullopt; // channel rotated against this face: not box-like

                    const Vec2d overlapMin{ std::max(notchMin.X, 0.0f), std::max(notchMin.Y, 0.0f) };
                    const Vec2d overlapMax{ std::min(notchMax.X, faceFrame->Width),
                                            std::min(notchMax.Y, faceFrame->Height) };
                    if (overlapMax.X - overlapMin.X <= kCarveShapeTol
                        || overlapMax.Y - overlapMin.Y <= kCarveShapeTol)
                        continue; // touching only: not crossed

                    NeighborCut cut;
                    cut.Face = f;
                    cut.Host = { mesh.Faces[f].Loop[0], mesh.Faces[f].Loop[1],
                                 mesh.Faces[f].Loop[2], mesh.Faces[f].Loop[3] };
                    cut.Flush = FrameFlushSides(*faceFrame, overlapMin, overlapMax);
                    cut.FullyCovered =
                        cut.Flush[0] && cut.Flush[1] && cut.Flush[2] && cut.Flush[3];
                    const std::array<Vec2d, 4> overlapUv = RectUvCorners(overlapMin, overlapMax);
                    for (int c = 0; c < 4; ++c)
                        cut.Rect[static_cast<std::size_t>(c)] = mintOrReuse(
                            faceFrame->Origin
                            + faceFrame->AxisU * overlapUv[static_cast<std::size_t>(c)].X
                            + faceFrame->AxisV * overlapUv[static_cast<std::size_t>(c)].Y);
                    coveredArea += static_cast<double>(overlapMax.X - overlapMin.X)
                                 * static_cast<double>(overlapMax.Y - overlapMin.Y);
                    claimed.insert(f);
                    neighborCuts.push_back(cut);
                }
                else
                {
                    // A non-rectangular face on the plane: reject only if the
                    // channel actually reaches it (bounding boxes overlap in
                    // the plane frame).
                    Vec2d faceMin{ std::numeric_limits<float>::max(),
                                   std::numeric_limits<float>::max() };
                    Vec2d faceMax{ std::numeric_limits<float>::lowest(),
                                   std::numeric_limits<float>::lowest() };
                    for (std::uint32_t v : mesh.Faces[f].Loop)
                    {
                        const Vec3d rel = mesh.Vertices[v].Position - planeFrame->Origin;
                        const Vec2d uv{ rel.Dot(planeFrame->AxisU), rel.Dot(planeFrame->AxisV) };
                        faceMin.X = std::min(faceMin.X, uv.X);
                        faceMin.Y = std::min(faceMin.Y, uv.Y);
                        faceMax.X = std::max(faceMax.X, uv.X);
                        faceMax.Y = std::max(faceMax.Y, uv.Y);
                    }
                    Vec2d channelMin{ std::numeric_limits<float>::max(),
                                      std::numeric_limits<float>::max() };
                    Vec2d channelMax{ std::numeric_limits<float>::lowest(),
                                      std::numeric_limits<float>::lowest() };
                    for (const Vec3d& corner : notchCorners)
                    {
                        const Vec3d rel = corner - planeFrame->Origin;
                        const Vec2d uv{ rel.Dot(planeFrame->AxisU), rel.Dot(planeFrame->AxisV) };
                        channelMin.X = std::min(channelMin.X, uv.X);
                        channelMin.Y = std::min(channelMin.Y, uv.Y);
                        channelMax.X = std::max(channelMax.X, uv.X);
                        channelMax.Y = std::max(channelMax.Y, uv.Y);
                    }
                    if (std::min(channelMax.X, faceMax.X) - std::max(channelMin.X, faceMin.X)
                            > kCarveShapeTol
                        && std::min(channelMax.Y, faceMax.Y) - std::max(channelMin.Y, faceMin.Y)
                            > kCarveShapeTol)
                        return std::nullopt;
                }
            }

            // The consumed faces must tile the whole channel: a gap means the
            // side surface does not actually span source to target here.
            if (std::abs(coveredArea - notchArea) > notchArea * 1e-3 + 1e-6)
                return std::nullopt;
        }

        // Only sides that actually open the boundary can disconnect the brush.
        const std::array<bool, 4> openSide = {
            flush[0] && !seamSide[0],
            flush[1] && !seamSide[1],
            flush[2] && !seamSide[2],
            flush[3] && !seamSide[3],
        };
        if (openSide[0] && openSide[1] && openSide[2] && openSide[3])
            return std::nullopt; // opens every side: nothing bounds the result
        if ((openSide[0] && openSide[2] && !openSide[1] && !openSide[3])
            || (openSide[1] && openSide[3] && !openSide[0] && !openSide[2]))
            return std::nullopt; // an opposite-pair-only channel splits the brush in two

        // Target rect corner slots in the TARGET frame's own order, for its
        // ring emission (the projected order is mirrored, not reusable).
        const std::optional<std::array<std::uint32_t, 4>> targetSlots =
            MatchRectCornerSlots(out, targetFrame, target.RectMin, target.RectMax, targetIdx);
        if (!targetSlots.has_value())
            return std::nullopt;

        // All checks passed: erase the source, target, and neighbor faces in
        // one descending pass, then append the rebuilt pieces.
        std::vector<std::uint32_t> doomed = { face, target.Face };
        for (const NeighborCut& cut : neighborCuts)
            doomed.push_back(cut.Face);
        std::sort(doomed.begin(), doomed.end(), std::greater<>());
        const FaceMaterial sourceMaterial = mesh.Faces[face].Material;
        const FaceMaterial targetMaterial = mesh.Faces[target.Face].Material;
        for (std::uint32_t f : doomed)
            out.Faces.erase(out.Faces.begin() + static_cast<std::ptrdiff_t>(f));

        const auto appendQuad = [&](std::array<std::uint32_t, 4> loop, const FaceMaterial& material)
        {
            BrushFace piece;
            piece.Loop.assign(loop.begin(), loop.end());
            piece.Material = material;
            piece.Normal = BrushComputeFaceNormal(out, piece);
            out.Faces.push_back(std::move(piece));
        };
        const auto appendRing = [&](const std::array<std::uint32_t, 4>& ringHost,
                                    const std::array<std::uint32_t, 4>& rect,
                                    const std::array<bool, 4>& ringFlush,
                                    const FaceMaterial& material)
        {
            for (int k = 0; k < 4; ++k)
                if (!ringFlush[static_cast<std::size_t>(k)])
                    appendQuad({ ringHost[static_cast<std::size_t>(k)],
                                 ringHost[static_cast<std::size_t>((k + 1) % 4)],
                                 rect[static_cast<std::size_t>((k + 1) % 4)],
                                 rect[static_cast<std::size_t>(k)] },
                               material);
        };

        appendRing(host, rectIdx, flush, sourceMaterial);
        appendRing(targetHost, *targetSlots, targetFlush, targetMaterial);
        for (const NeighborCut& cut : neighborCuts)
            if (!cut.FullyCovered)
                appendRing(cut.Host, cut.Rect, cut.Flush, mesh.Faces[cut.Face].Material);

        // Tunnel walls everywhere except the open sides: non-flush sides and
        // seam sides (where the surface continues past the flush edge) both
        // bound the channel with a wall. The walls are perpendicular to the
        // cap, so the cap's projection axes would stretch edge-on: re-derive
        // axes per wall normal, keeping scale/offset/rotation (the same rule
        // as BridgeCapsIntoTunnel and ExtrudeFaceAlong).
        for (int k = 0; k < 4; ++k)
        {
            if (openSide[static_cast<std::size_t>(k)])
                continue;
            const std::size_t i = static_cast<std::size_t>(k);
            const std::size_t j = static_cast<std::size_t>((k + 1) % 4);
            BrushFace wall;
            wall.Loop = { rectIdx[i], rectIdx[j], targetIdx[j], targetIdx[i] };
            wall.Material = sourceMaterial;
            wall.Normal = BrushComputeFaceNormal(out, wall);
            wall.Material.Uv = UvProjectionForNormal(wall.Normal, sourceMaterial.Uv.WorldAligned);
            wall.Material.Uv.Scale = sourceMaterial.Uv.Scale;
            wall.Material.Uv.Offset = sourceMaterial.Uv.Offset;
            wall.Material.Uv.Rotation = sourceMaterial.Uv.Rotation;
            out.Faces.push_back(std::move(wall));
        }

        // A channel across subdivided planes mints crossing vertices on other
        // faces' edges (chain crossings, seam corners on a coplanar neighbor);
        // absorb them everywhere as collinear loop vertices so no edge is left
        // T-junctioned.
        for (std::size_t f = 0; f < out.Faces.size(); ++f)
            AbsorbCollinearVertices(out, static_cast<std::uint32_t>(f));
        return out;
    }

    // Closed solid: every undirected edge shared by exactly two faces.
    bool MeshIsClosed(const BrushMesh& mesh)
    {
        const EdgeFaces edgeFaces = BuildEdgeFaces(mesh);
        for (const auto& entry : edgeFaces)
            if (entry.second.size() != 2)
                return false;
        return true;
    }
}

BrushMesh BrushOps::CarveFaceRectThrough(const BrushMesh& mesh, std::uint32_t face,
                                         Vec2d rectMin, Vec2d rectMax)
{
    const std::optional<BrushRectFaceFrame> sourceFrame = RectFaceFrame(mesh, face);
    if (!sourceFrame.has_value())
        return mesh;

    // Canonicalize with the same snap-to-flush the plain carve uses.
    float u0 = std::clamp(std::min(rectMin.X, rectMax.X), 0.0f, sourceFrame->Width);
    float u1 = std::clamp(std::max(rectMin.X, rectMax.X), 0.0f, sourceFrame->Width);
    float v0 = std::clamp(std::min(rectMin.Y, rectMax.Y), 0.0f, sourceFrame->Height);
    float v1 = std::clamp(std::max(rectMin.Y, rectMax.Y), 0.0f, sourceFrame->Height);
    const auto snap = [](float& x, float limit)
    {
        if (x <= kCarveSnapTol)
            x = 0.0f;
        if (x >= limit - kCarveSnapTol)
            x = limit;
    };
    snap(u0, sourceFrame->Width);
    snap(u1, sourceFrame->Width);
    snap(v0, sourceFrame->Height);
    snap(v1, sourceFrame->Height);
    if (u1 - u0 <= kCarveSnapTol || v1 - v0 <= kCarveSnapTol)
        return mesh;
    const Vec2d sourceMin{ u0, v0 };
    const Vec2d sourceMax{ u1, v1 };

    const std::array<bool, 4> flush = FrameFlushSides(*sourceFrame, sourceMin, sourceMax);
    const bool anyFlush = flush[0] || flush[1] || flush[2] || flush[3];
    // Which flush sides disconnect the brush depends on whether each one opens
    // the boundary or seams against a continuing surface; the flush kernel
    // classifies and rejects accordingly.

    const Vec3d sourceNormal = BrushComputeFaceNormal(mesh, mesh.Faces[face]);
    if (sourceNormal.SqrMagnitude() <= 0.0f)
        return mesh;

    const std::array<Vec2d, 4> sourceUv = RectUvCorners(sourceMin, sourceMax);
    const std::array<Vec3d, 4> sourceCorners = RectFrameCorners(*sourceFrame, sourceUv);
    const std::optional<ThroughFaceCandidate> target =
        FindThroughFace(mesh, face, sourceNormal, sourceCorners, /*allowFlush*/ anyFlush);
    if (!target.has_value())
    {
        // An open host (a plane) has no opposite face to tunnel to; there the
        // pierce degrades to punching the rect out of the face itself. Closed
        // solids keep the refusal: a blind hole would silently open the solid.
        if (MeshIsClosed(mesh))
            return mesh;
        BrushMesh out = CarveFaceRect(mesh, face, sourceMin, sourceMax);
        if (out.Faces.size() <= mesh.Faces.size())
            return mesh;
        out.Faces.pop_back(); // the center rect is appended last; removing it opens the hole
        return out;
    }

    if (anyFlush)
    {
        const std::optional<BrushRectFaceFrame> targetFrame = RectFaceFrame(mesh, target->Face);
        if (!targetFrame.has_value())
            return mesh;
        std::optional<BrushMesh> notched = CarveThroughWithFlushSides(
            mesh, face, *sourceFrame, sourceMin, sourceMax, flush, *target, *targetFrame);
        return notched.has_value() ? std::move(*notched) : mesh;
    }

    BrushMesh out = CarveFaceRect(mesh, face, sourceMin, sourceMax);
    if (out.Faces.size() <= mesh.Faces.size())
        return mesh;

    std::uint32_t targetFace = target->Face;
    if (targetFace > face)
        --targetFace;
    std::uint32_t sourceCenterFace = static_cast<std::uint32_t>(out.Faces.size() - 1);

    out = CarveFaceRect(out, targetFace, target->RectMin, target->RectMax);
    if (out.Faces.size() <= mesh.Faces.size() + 4u)
        return mesh;
    if (targetFace < sourceCenterFace)
        --sourceCenterFace;
    const std::uint32_t targetCenterFace = static_cast<std::uint32_t>(out.Faces.size() - 1);
    if (sourceCenterFace >= out.Faces.size() || targetCenterFace >= out.Faces.size())
        return mesh;

    std::optional<BrushMesh> tunnel = BridgeCapsIntoTunnel(std::move(out),
                                                           sourceCenterFace, targetCenterFace,
                                                           *sourceFrame, sourceMin, sourceMax,
                                                           target->Projected, mesh.Faces[face].Material);
    return tunnel.has_value() ? std::move(*tunnel) : mesh;
}

BrushMesh BrushOps::InsertFaceLoopBoundsThrough(const BrushMesh& mesh, std::uint32_t face,
                                                Vec2d rectMin, Vec2d rectMax)
{
    const std::optional<BrushRectFaceFrame> sourceFrame = RectFaceFrame(mesh, face);
    if (!sourceFrame.has_value())
        return mesh;

    // Canonicalize with the same snap-to-flush the carves use.
    float u0 = std::clamp(std::min(rectMin.X, rectMax.X), 0.0f, sourceFrame->Width);
    float u1 = std::clamp(std::max(rectMin.X, rectMax.X), 0.0f, sourceFrame->Width);
    float v0 = std::clamp(std::min(rectMin.Y, rectMax.Y), 0.0f, sourceFrame->Height);
    float v1 = std::clamp(std::max(rectMin.Y, rectMax.Y), 0.0f, sourceFrame->Height);
    const auto snap = [](float& x, float limit)
    {
        if (x <= kCarveSnapTol)
            x = 0.0f;
        if (x >= limit - kCarveSnapTol)
            x = limit;
    };
    snap(u0, sourceFrame->Width);
    snap(u1, sourceFrame->Width);
    snap(v0, sourceFrame->Height);
    snap(v1, sourceFrame->Height);
    if (u1 - u0 <= kCarveSnapTol || v1 - v0 <= kCarveSnapTol)
        return mesh;
    const Vec2d sourceMin{ u0, v0 };
    const Vec2d sourceMax{ u1, v1 };

    const std::array<bool, 4> flush = FrameFlushSides(*sourceFrame, sourceMin, sourceMax);
    const bool anyFlush = flush[0] || flush[1] || flush[2] || flush[3];

    const Vec3d sourceNormal = BrushComputeFaceNormal(mesh, mesh.Faces[face]);
    if (sourceNormal.SqrMagnitude() <= 0.0f)
        return mesh;

    // Resolve the opposite face, its frame, and the flush neighbors' frames
    // BEFORE the topology changes: the loop cuts re-split faces (indices
    // shift) but move no existing vertex, so captured frames and projected
    // corners stay geometrically valid.
    const std::array<Vec3d, 4> sourceCorners =
        RectFrameCorners(*sourceFrame, RectUvCorners(sourceMin, sourceMax));
    const std::optional<ThroughFaceCandidate> target =
        FindThroughFace(mesh, face, sourceNormal, sourceCorners, /*allowFlush*/ anyFlush);
    if (!target.has_value())
    {
        // Open host (a plane): no opposite face to tunnel to, so the pierce
        // degrades to cutting the loops and removing the bounded rect face.
        // Closed solids keep the refusal (same rule as CarveFaceRectThrough).
        if (MeshIsClosed(mesh))
            return mesh;
        BrushMesh out = InsertFaceLoopBounds(mesh, face, sourceMin, sourceMax);
        if (out.Faces.size() == mesh.Faces.size() && out.Vertices.size() == mesh.Vertices.size())
            return mesh;
        const std::optional<std::uint32_t> cap =
            FindRectFaceInFrame(out, *sourceFrame, sourceMin, sourceMax);
        if (!cap.has_value())
            return mesh;
        out.Faces.erase(out.Faces.begin() + static_cast<std::ptrdiff_t>(*cap));
        return out;
    }
    const std::optional<BrushRectFaceFrame> targetFrame = RectFaceFrame(mesh, target->Face);
    if (!targetFrame.has_value())
        return mesh;
    if (anyFlush)
    {
        const std::array<bool, 4> targetFlush =
            FrameFlushSides(*targetFrame, target->RectMin, target->RectMax);
        const int sourceCount = static_cast<int>(flush[0]) + static_cast<int>(flush[1])
                              + static_cast<int>(flush[2]) + static_cast<int>(flush[3]);
        const int targetCount = static_cast<int>(targetFlush[0]) + static_cast<int>(targetFlush[1])
                              + static_cast<int>(targetFlush[2]) + static_cast<int>(targetFlush[3]);
        if (sourceCount != targetCount)
            return mesh;
    }

    // Each flush side's notch strip lies on the face bordering that side's
    // host edge: capture its frame and notch rect now, find the strip face
    // after the cuts.
    struct NotchStrip
    {
        BrushRectFaceFrame Frame{};
        Vec2d Min = {};
        Vec2d Max = {};
    };
    std::vector<NotchStrip> strips;
    std::array<bool, 4> openSide{};
    if (anyFlush)
    {
        const EdgeFaces edgeFaces = BuildEdgeFaces(mesh);
        const std::array<Vec3d, 4> rectCorners =
            RectFrameCorners(*sourceFrame, RectUvCorners(sourceMin, sourceMax));
        const std::vector<std::uint32_t>& hostLoop = mesh.Faces[face].Loop;
        const std::vector<std::uint32_t>& targetHostLoop = mesh.Faces[target->Face].Loop;
        if (hostLoop.size() != 4)
            return mesh;
        const Vec3d targetNormal = BrushComputeFaceNormal(mesh, mesh.Faces[target->Face]);
        for (int k = 0; k < 4; ++k)
        {
            if (!flush[k])
                continue;

            const Vec3d sourceP0 = rectCorners[static_cast<std::size_t>(k)];
            const Vec3d sourceP1 = rectCorners[static_cast<std::size_t>((k + 1) % 4)];
            const Vec3d targetP0 = target->Projected[static_cast<std::size_t>(k)];
            const Vec3d targetP1 = target->Projected[static_cast<std::size_t>((k + 1) % 4)];

            const std::optional<std::uint32_t> sourceNeighbor =
                NeighborAcrossSegment(mesh, edgeFaces, face, hostLoop, sourceP0, sourceP1);
            const std::optional<std::uint32_t> targetNeighbor =
                NeighborAcrossSegment(mesh, edgeFaces, target->Face, targetHostLoop, targetP0, targetP1);
            if (!sourceNeighbor.has_value() || !targetNeighbor.has_value())
                return mesh;

            // Seam sides (a coplanar neighbor: the surface continues past the
            // flush edge) keep their wall and cut nothing; only a bent
            // boundary opens a notch. Half-seam channels need a real volume
            // boolean: refused.
            const bool sourceSeam = std::abs(
                BrushComputeFaceNormal(mesh, mesh.Faces[*sourceNeighbor]).Dot(sourceNormal)) > 0.99f;
            const bool targetSeam = std::abs(
                BrushComputeFaceNormal(mesh, mesh.Faces[*targetNeighbor]).Dot(targetNormal)) > 0.99f;
            if (sourceSeam != targetSeam)
                return mesh;
            if (sourceSeam)
                continue;
            openSide[static_cast<std::size_t>(k)] = true;

            if (*sourceNeighbor == target->Face)
                return mesh;
            const std::optional<BrushRectFaceFrame> neighborFrame = RectFaceFrame(mesh, *sourceNeighbor);
            if (!neighborFrame.has_value())
                return mesh;

            // Notch corners: the flush side's rect corners and their target
            // projections, expressed in the neighbor's frame.
            const Vec3d notchCorners[4] = { sourceP0, sourceP1, targetP0, targetP1 };
            const Vec3d neighborNormal = neighborFrame->AxisU.Cross(neighborFrame->AxisV);
            NotchStrip strip;
            strip.Frame = *neighborFrame;
            strip.Min = { std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
            strip.Max = { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
            for (const Vec3d& corner : notchCorners)
            {
                const Vec3d rel = corner - neighborFrame->Origin;
                if (std::abs(rel.Dot(neighborNormal)) > kCarveShapeTol)
                    return mesh; // corner off the neighbor plane: not a box-like side
                const Vec2d uv{ rel.Dot(neighborFrame->AxisU), rel.Dot(neighborFrame->AxisV) };
                strip.Min.X = std::min(strip.Min.X, uv.X);
                strip.Min.Y = std::min(strip.Min.Y, uv.Y);
                strip.Max.X = std::max(strip.Max.X, uv.X);
                strip.Max.Y = std::max(strip.Max.Y, uv.Y);
            }
            strips.push_back(strip);
        }

        // Only sides that actually open the boundary can disconnect the brush.
        if (openSide[0] && openSide[1] && openSide[2] && openSide[3])
            return mesh; // opens every side: nothing bounds the result
        if ((openSide[0] && openSide[2] && !openSide[1] && !openSide[3])
            || (openSide[1] && openSide[3] && !openSide[0] && !openSide[2]))
            return mesh; // an opposite-pair-only channel splits the brush in two
    }

    // Full wrapping loops; where they cross the source and opposite faces they
    // mint vertices exactly at the two rects' corners, so the openings align
    // by construction.
    BrushMesh out = InsertFaceLoopBounds(mesh, face, sourceMin, sourceMax);
    if (out.Faces.size() == mesh.Faces.size() && out.Vertices.size() == mesh.Vertices.size())
        return mesh;

    const std::optional<std::uint32_t> sourceCap =
        FindRectFaceInFrame(out, *sourceFrame, sourceMin, sourceMax);
    const std::optional<std::uint32_t> targetCap =
        FindRectFaceInFrame(out, *targetFrame, target->RectMin, target->RectMax);
    if (!sourceCap.has_value() || !targetCap.has_value() || *sourceCap == *targetCap)
        return mesh; // the loops did not reach the opposite face cleanly

    // Delete every face inside each flush side's notch region so the channel
    // opens there. The wrapping loops already cut the neighbors into strips,
    // and pre-existing crossing loops may have tiled the notch into several;
    // consume them all, and require them to tile the whole notch. Descending
    // order keeps the collected indices valid; the cap indices are re-resolved
    // afterwards.
    if (!strips.empty())
    {
        std::vector<std::uint32_t> doomed;
        for (const NotchStrip& strip : strips)
        {
            const Vec3d normal = strip.Frame.AxisU.Cross(strip.Frame.AxisV);
            const double notchArea = static_cast<double>(strip.Max.X - strip.Min.X)
                                   * static_cast<double>(strip.Max.Y - strip.Min.Y);
            double covered = 0.0;
            for (std::uint32_t f = 0; f < out.Faces.size(); ++f)
            {
                if (f == *sourceCap || f == *targetCap
                    || std::find(doomed.begin(), doomed.end(), f) != doomed.end())
                    continue;
                bool inside = true;
                Vec2d faceMin{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
                Vec2d faceMax{ std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
                for (std::uint32_t v : out.Faces[f].Loop)
                {
                    const Vec3d rel = out.Vertices[v].Position - strip.Frame.Origin;
                    if (std::abs(rel.Dot(normal)) > kCarveShapeTol)
                    {
                        inside = false;
                        break;
                    }
                    const Vec2d uv{ rel.Dot(strip.Frame.AxisU), rel.Dot(strip.Frame.AxisV) };
                    if (uv.X < strip.Min.X - kCarveShapeTol || uv.X > strip.Max.X + kCarveShapeTol
                        || uv.Y < strip.Min.Y - kCarveShapeTol || uv.Y > strip.Max.Y + kCarveShapeTol)
                    {
                        inside = false;
                        break;
                    }
                    faceMin.X = std::min(faceMin.X, uv.X);
                    faceMin.Y = std::min(faceMin.Y, uv.Y);
                    faceMax.X = std::max(faceMax.X, uv.X);
                    faceMax.Y = std::max(faceMax.Y, uv.Y);
                }
                if (!inside)
                    continue;
                covered += static_cast<double>(faceMax.X - faceMin.X)
                         * static_cast<double>(faceMax.Y - faceMin.Y);
                doomed.push_back(f);
            }
            if (std::abs(covered - notchArea) > notchArea * 1e-3 + 1e-6)
                return mesh; // the strips do not tile the notch: leave it alone
        }
        std::sort(doomed.begin(), doomed.end(), std::greater<>());
        for (std::uint32_t f : doomed)
            out.Faces.erase(out.Faces.begin() + static_cast<std::ptrdiff_t>(f));
    }

    const std::optional<std::uint32_t> sourceCapAfter =
        FindRectFaceInFrame(out, *sourceFrame, sourceMin, sourceMax);
    const std::optional<std::uint32_t> targetCapAfter =
        FindRectFaceInFrame(out, *targetFrame, target->RectMin, target->RectMax);
    if (!sourceCapAfter.has_value() || !targetCapAfter.has_value())
        return mesh;

    std::optional<BrushMesh> tunnel = BridgeCapsIntoTunnel(std::move(out),
                                                           *sourceCapAfter, *targetCapAfter,
                                                           *sourceFrame, sourceMin, sourceMax,
                                                           target->Projected, mesh.Faces[face].Material,
                                                           /*skipWallSide*/ openSide);
    if (!tunnel.has_value())
        return mesh;

    // Pre-existing loops crossing the channel leave vertices on the new wall
    // edges; absorb them so no edge is left T-junctioned. The walls are the
    // last appended faces (one per non-open side).
    const int wallCount = 4 - (static_cast<int>(openSide[0]) + static_cast<int>(openSide[1])
                               + static_cast<int>(openSide[2]) + static_cast<int>(openSide[3]));
    for (int w = 0; w < wallCount && w < static_cast<int>(tunnel->Faces.size()); ++w)
        AbsorbCollinearVertices(*tunnel,
                                static_cast<std::uint32_t>(tunnel->Faces.size() - 1 - static_cast<std::size_t>(w)));
    return std::move(*tunnel);
}

namespace
{
    // Region extrude shared by the normal-blend and directional variants:
    // mints one moved duplicate per region vertex at `offsetOf(v)`, walls only
    // the boundary edges of the selected region (interior edges shared by two
    // selected faces move as one shell), then retargets the caps. Wall
    // materials continue the unselected neighbor across each boundary edge
    // with UV axes re-derived per wall normal (the ExtrudeFaceAlong rule).
    template <typename OffsetOf>
    BrushMesh ExtrudeFaceRegion(const BrushMesh& mesh,
                                const std::vector<std::uint32_t>& region,
                                const std::vector<bool>& selected,
                                const OffsetOf& offsetOf)
    {
        BrushMesh out = mesh;

        // Moved duplicates, minted in region traversal order (deterministic).
        constexpr std::uint32_t kUnmoved = std::numeric_limits<std::uint32_t>::max();
        std::vector<std::uint32_t> moved(mesh.Vertices.size(), kUnmoved);
        for (std::uint32_t f : region)
            for (std::uint32_t v : mesh.Faces[f].Loop)
            {
                if (moved[v] != kUnmoved)
                    continue;
                moved[v] = static_cast<std::uint32_t>(out.Vertices.size());
                out.Vertices.push_back(BrushVertex{ mesh.Vertices[v].Position + offsetOf(v) });
            }

        // Walls on the region boundary only, before the caps are retargeted
        // (the walls span old ring -> moved ring).
        const EdgeFaces edgeFaces = BuildEdgeFaces(mesh);
        for (std::uint32_t f : region)
        {
            const std::vector<std::uint32_t>& baseLoop = mesh.Faces[f].Loop;
            const std::size_t n = baseLoop.size();
            for (std::size_t i = 0; i < n; ++i)
            {
                const std::uint32_t a = baseLoop[i];
                const std::uint32_t b = baseLoop[(i + 1) % n];
                const auto it = edgeFaces.find(UndirectedEdge(a, b));
                if (it == edgeFaces.end())
                    continue;

                bool interior = false;
                const BrushFace* seed = nullptr;
                for (const auto& [neighbor, unusedEdgeIndex] : it->second)
                {
                    if (neighbor == f)
                        continue;
                    if (selected[neighbor])
                        interior = true;
                    else if (seed == nullptr)
                        seed = &mesh.Faces[neighbor];
                }
                if (interior)
                    continue;

                BrushFace wall;
                wall.Loop = { a, b, moved[b], moved[a] };
                const FaceMaterial& material = seed != nullptr ? seed->Material : mesh.Faces[f].Material;
                wall.Material.Material = material.Material;
                wall.Normal = BrushComputeFaceNormal(out, wall);

                // Coplanar with the continuing neighbor: carry its projection
                // whole so the texture flows onto the new strip. Otherwise
                // re-derive the axes for the wall's own plane.
                const Vec3d seedNormal = seed != nullptr ? BrushComputeFaceNormal(mesh, *seed)
                                                         : Vec3d{ 0.0f, 0.0f, 0.0f };
                const bool coplanar = seed != nullptr
                    && wall.Normal.SqrMagnitude() > 0.0f
                    && seedNormal.SqrMagnitude() > 0.0f
                    && std::abs(wall.Normal.Normalized().Dot(seedNormal.Normalized())) > 0.999f;
                if (coplanar)
                {
                    wall.Material.Uv = material.Uv;
                }
                else
                {
                    wall.Material.Uv = UvProjectionForNormal(wall.Normal, material.Uv.WorldAligned);
                    wall.Material.Uv.Scale = material.Uv.Scale;
                    wall.Material.Uv.Offset = material.Uv.Offset;
                    wall.Material.Uv.Rotation = material.Uv.Rotation;
                }
                out.Faces.push_back(std::move(wall));
            }
        }

        for (std::uint32_t f : region)
            for (std::uint32_t& v : out.Faces[f].Loop)
                v = moved[v];

        BrushValidateAndRepair(out);
        return out;
    }

    // Validated, deduplicated region in first-seen order plus a selection mask.
    bool GatherFaceRegion(const BrushMesh& mesh,
                          std::span<const std::uint32_t> faces,
                          std::vector<std::uint32_t>& region,
                          std::vector<bool>& selected)
    {
        selected.assign(mesh.Faces.size(), false);
        for (std::uint32_t f : faces)
        {
            if (f >= mesh.Faces.size() || selected[f])
                continue;
            selected[f] = true;
            region.push_back(f);
        }
        return !region.empty();
    }
}

BrushMesh BrushOps::ExtrudeFacesAlongNormals(const BrushMesh& mesh,
                                             std::span<const std::uint32_t> faces,
                                             float distance)
{
    std::vector<std::uint32_t> region;
    std::vector<bool> selected;
    if (!GatherFaceRegion(mesh, faces, region, selected) || distance == 0.0f)
        return mesh;

    // Blended normal per region vertex: the normalized sum of the unit
    // normals of the selected faces using it.
    std::vector<Vec3d> vertexNormal(mesh.Vertices.size(), Vec3d{ 0.0f, 0.0f, 0.0f });
    for (std::uint32_t f : region)
    {
        const Vec3d normal = BrushComputeFaceNormal(mesh, mesh.Faces[f]);
        if (normal.SqrMagnitude() <= 0.0f)
            return mesh; // degenerate cap: nothing sensible to offset along
        for (std::uint32_t v : mesh.Faces[f].Loop)
            vertexNormal[v] += normal.Normalized();
    }

    return ExtrudeFaceRegion(mesh, region, selected, [&](std::uint32_t v)
    {
        const Vec3d sum = vertexNormal[v];
        return sum.SqrMagnitude() > 0.0f ? sum.Normalized() * distance
                                         : Vec3d{ 0.0f, 0.0f, 0.0f };
    });
}

BrushMesh BrushOps::ExtrudeFacesAlong(const BrushMesh& mesh,
                                      std::span<const std::uint32_t> faces,
                                      Vec3d offset)
{
    std::vector<std::uint32_t> region;
    std::vector<bool> selected;
    if (!GatherFaceRegion(mesh, faces, region, selected) || offset.SqrMagnitude() <= 0.0f)
        return mesh;
    return ExtrudeFaceRegion(mesh, region, selected, [&](std::uint32_t) { return offset; });
}

BrushMesh BrushOps::BevelEdges(const BrushMesh& mesh,
                               std::span<const std::array<std::uint32_t, 2>> edges,
                               float width,
                               int segments)
{
    segments = std::max(1, segments);
    if (width <= 0.0f || edges.empty())
        return mesh;

    // Canonical selected edges, deduplicated, in first-seen order.
    std::vector<UndirectedEdge> sel;
    for (const std::array<std::uint32_t, 2>& e : edges)
    {
        if (e[0] == e[1] || e[0] >= mesh.Vertices.size() || e[1] >= mesh.Vertices.size())
            continue;
        const UndirectedEdge canonical(e[0], e[1]);
        if (std::find_if(sel.begin(), sel.end(), [&](const UndirectedEdge& s)
                         { return s.U == canonical.U && s.V == canonical.V; }) == sel.end())
            sel.push_back(canonical);
    }
    if (sel.empty())
        return mesh;

    std::vector<Vec3d> faceNormal(mesh.Faces.size());
    for (std::size_t f = 0; f < mesh.Faces.size(); ++f)
        faceNormal[f] = BrushComputeFaceNormal(mesh, mesh.Faces[f]).Normalized();

    // Each selected edge must be manifold, and its faces must actually bend
    // (a coplanar pair has no profile to round).
    struct BevelEdge
    {
        std::uint32_t U = 0;
        std::uint32_t V = 0;
        std::uint32_t SideA = 0; // face index
        std::uint32_t SideB = 0;
    };
    const EdgeFaces edgeFaces = BuildEdgeFaces(mesh);
    std::vector<BevelEdge> chain;
    chain.reserve(sel.size());
    for (const UndirectedEdge& e : sel)
    {
        const auto it = edgeFaces.find(e);
        if (it == edgeFaces.end() || it->second.size() != 2)
            return mesh;
        const std::uint32_t f0 = it->second[0].first;
        const std::uint32_t f1 = it->second[1].first;
        if (std::abs(faceNormal[f0].Dot(faceNormal[f1])) > 0.999f)
            return mesh;
        chain.push_back(BevelEdge{ e.U, e.V, f0, f1 });
    }

    // A vertex fanned by 3+ selected edges has no single profile: refused.
    std::vector<std::vector<std::uint32_t>> incident(mesh.Vertices.size());
    for (std::uint32_t i = 0; i < chain.size(); ++i)
    {
        incident[chain[i].U].push_back(i);
        incident[chain[i].V].push_back(i);
    }
    for (const std::vector<std::uint32_t>& list : incident)
        if (list.size() > 2)
            return mesh;

    // Chain-consistent side assignment: walking across a shared vertex keeps
    // side A on the face (or failing an exact match, the closest normal), so
    // one side of the whole run replaces with row 0 and the other with row N.
    {
        std::vector<bool> oriented(chain.size(), false);
        std::vector<std::uint32_t> stack;
        for (std::uint32_t seed = 0; seed < chain.size(); ++seed)
        {
            if (oriented[seed])
                continue;
            oriented[seed] = true;
            stack.push_back(seed);
            while (!stack.empty())
            {
                const std::uint32_t i = stack.back();
                stack.pop_back();
                for (const std::uint32_t v : { chain[i].U, chain[i].V })
                    for (const std::uint32_t j : incident[v])
                    {
                        if (j == i || oriented[j])
                            continue;
                        // A face shared by both edges (a rim loop's cap) must
                        // carry the same side label on both; only unshared
                        // sides fall back to normal similarity.
                        bool swapSides;
                        if (chain[j].SideA == chain[i].SideA || chain[j].SideB == chain[i].SideB)
                            swapSides = false;
                        else if (chain[j].SideA == chain[i].SideB || chain[j].SideB == chain[i].SideA)
                            swapSides = true;
                        else
                        {
                            const float keep = faceNormal[chain[j].SideA].Dot(faceNormal[chain[i].SideA])
                                             + faceNormal[chain[j].SideB].Dot(faceNormal[chain[i].SideB]);
                            const float crossed = faceNormal[chain[j].SideA].Dot(faceNormal[chain[i].SideB])
                                                + faceNormal[chain[j].SideB].Dot(faceNormal[chain[i].SideA]);
                            swapSides = crossed > keep;
                        }
                        if (swapSides)
                            std::swap(chain[j].SideA, chain[j].SideB);
                        oriented[j] = true;
                        stack.push_back(j);
                    }
            }
        }
    }

    // In-plane retreat direction at `v` away from edge (v -> other), inside
    // face `f`: the face-centroid direction with the edge component removed.
    const auto sideDirection = [&](std::uint32_t v, std::uint32_t other, std::uint32_t f)
        -> std::optional<Vec3d>
    {
        const Vec3d edgeDir = (mesh.Vertices[other].Position - mesh.Vertices[v].Position);
        if (edgeDir.SqrMagnitude() <= 0.0f)
            return std::nullopt;
        const Vec3d unit = edgeDir.Normalized();
        const Vec3d toCentroid = BrushFaceCentroid(mesh, mesh.Faces[f]) - mesh.Vertices[v].Position;
        const Vec3d inPlane = toCentroid - unit * toCentroid.Dot(unit);
        if (inPlane.SqrMagnitude() <= 1.0e-12f)
            return std::nullopt;
        return inPlane.Normalized();
    };

    // Profile rows per chain vertex: row 0 sits `width` into side A, row N
    // `width` into side B, intermediate rows on the normalized blend (a
    // circular arc of radius `width` about the original vertex).
    BrushMesh out = mesh;
    std::vector<std::vector<std::uint32_t>> rows(mesh.Vertices.size());
    for (std::uint32_t v = 0; v < mesh.Vertices.size(); ++v)
    {
        if (incident[v].empty())
            continue;
        Vec3d dirA{ 0.0f, 0.0f, 0.0f };
        Vec3d dirB{ 0.0f, 0.0f, 0.0f };
        for (std::uint32_t i : incident[v])
        {
            const std::uint32_t other = chain[i].U == v ? chain[i].V : chain[i].U;
            const std::optional<Vec3d> a = sideDirection(v, other, chain[i].SideA);
            const std::optional<Vec3d> b = sideDirection(v, other, chain[i].SideB);
            if (!a.has_value() || !b.has_value())
                return mesh;
            dirA += *a;
            dirB += *b;
        }
        if (dirA.SqrMagnitude() <= 1.0e-12f || dirB.SqrMagnitude() <= 1.0e-12f)
            return mesh;
        dirA = dirA.Normalized();
        dirB = dirB.Normalized();

        // Quadratic profile tangent to both faces: rows run from the face-A
        // retreat point to the face-B one, bulging toward the original corner
        // (the round). A constant-radius arc about the old vertex would dip
        // INTO the solid instead (the caved-in bevel).
        rows[v].reserve(static_cast<std::size_t>(segments) + 1);
        for (int r = 0; r <= segments; ++r)
        {
            const float t = static_cast<float>(r) / static_cast<float>(segments);
            const Vec3d offset = dirA * (width * (1.0f - t) * (1.0f - t))
                               + dirB * (width * t * t);
            rows[v].push_back(static_cast<std::uint32_t>(out.Vertices.size()));
            out.Vertices.push_back(BrushVertex{ mesh.Vertices[v].Position + offset });
        }
    }

    // Rewrite every face touching a beveled vertex. Side A faces retreat to
    // row 0, side B to row N; any other face (a chain-end cap) absorbs the
    // whole profile, ordered so the row nearest its preceding loop vertex
    // comes first.
    for (std::uint32_t f = 0; f < out.Faces.size(); ++f)
    {
        const std::vector<std::uint32_t>& loop = mesh.Faces[f].Loop;
        bool touches = false;
        for (std::uint32_t v : loop)
            if (v < rows.size() && !rows[v].empty())
                touches = true;
        if (!touches)
            continue;

        std::vector<std::uint32_t> rebuilt;
        rebuilt.reserve(loop.size() + static_cast<std::size_t>(segments));
        for (std::size_t i = 0; i < loop.size(); ++i)
        {
            const std::uint32_t v = loop[i];
            if (v >= rows.size() || rows[v].empty())
            {
                rebuilt.push_back(v);
                continue;
            }

            bool isA = false;
            bool isB = false;
            for (std::uint32_t e : incident[v])
            {
                isA |= chain[e].SideA == f;
                isB |= chain[e].SideB == f;
            }
            if (isA && isB)
                return mesh; // the chain folds both sides onto one face
            if (isA)
            {
                rebuilt.push_back(rows[v].front());
                continue;
            }
            if (isB)
            {
                rebuilt.push_back(rows[v].back());
                continue;
            }

            // Cap: insert the profile facing the loop's traversal direction.
            const Vec3d prev = mesh.Vertices[loop[(i + loop.size() - 1) % loop.size()]].Position;
            const Vec3d first = out.Vertices[rows[v].front()].Position;
            const Vec3d last = out.Vertices[rows[v].back()].Position;
            if ((first - prev).SqrMagnitude() <= (last - prev).SqrMagnitude())
                rebuilt.insert(rebuilt.end(), rows[v].begin(), rows[v].end());
            else
                rebuilt.insert(rebuilt.end(), rows[v].rbegin(), rows[v].rend());
        }
        out.Faces[f].Loop = std::move(rebuilt);
    }

    // Chamfer strips, wound to continue side A's traversal of the seed edge.
    for (const BevelEdge& e : chain)
    {
        std::uint32_t u = e.U;
        std::uint32_t v = e.V;
        const std::vector<std::uint32_t>& aLoop = mesh.Faces[e.SideA].Loop;
        for (std::size_t i = 0; i < aLoop.size(); ++i)
        {
            if (aLoop[i] == e.V && aLoop[(i + 1) % aLoop.size()] == e.U)
            {
                std::swap(u, v);
                break;
            }
            if (aLoop[i] == e.U && aLoop[(i + 1) % aLoop.size()] == e.V)
                break;
        }

        const FaceMaterial& material = mesh.Faces[e.SideA].Material;
        for (int r = 0; r < segments; ++r)
        {
            BrushFace strip;
            strip.Loop = {
                rows[v][static_cast<std::size_t>(r)],
                rows[u][static_cast<std::size_t>(r)],
                rows[u][static_cast<std::size_t>(r) + 1],
                rows[v][static_cast<std::size_t>(r) + 1],
            };
            strip.Material.Material = material.Material;
            strip.Normal = BrushComputeFaceNormal(out, strip);
            strip.Material.Uv = UvProjectionForNormal(strip.Normal, material.Uv.WorldAligned);
            strip.Material.Uv.Scale = material.Uv.Scale;
            strip.Material.Uv.Offset = material.Uv.Offset;
            strip.Material.Uv.Rotation = material.Uv.Rotation;
            out.Faces.push_back(std::move(strip));
        }
    }

    BrushValidateAndRepair(out);
    return out;
}
