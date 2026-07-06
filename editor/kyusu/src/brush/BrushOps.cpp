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
        if (!splitFaces.count(f))
            rebuilt.push_back(std::move(out.Faces[f]));
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
