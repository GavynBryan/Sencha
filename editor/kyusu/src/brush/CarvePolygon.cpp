#include "brush/CarvePolygon.h"

#include "brush/BrushOps.h"
#include "brush/CarveSurround.h"

#include <utility>

#include <algorithm>
#include <array>
#include <cmath>

CarveOutcome CarveOutcome::Success(CarveSuccess value)
{
    CarveOutcome outcome;
    outcome.Reason = CarveStatus::Ok;
    outcome.Carved = std::move(value);
    return outcome;
}

CarveOutcome CarveOutcome::Failure(CarveStatus status)
{
    CarveOutcome outcome;
    outcome.Reason = status == CarveStatus::Ok ? CarveStatus::TopologyFailure : status;
    return outcome;
}

namespace
{
float Distance(Vec2d a, Vec2d b)
{
    return std::sqrt((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y));
}

// Turns carve points into mesh vertices against a mesh it is allowed to change.
//
// A point on the boundary it is working against must become a vertex the
// neighbouring face shares, not a new one sitting on its edge, or the carve
// leaves a T-junction that cracks open under any later edit.
//
// The ring is carried rather than re-read from a face, because the through
// carve works against the boundary of several merged coplanar faces, which is
// not any one face's loop. Splitting an edge inserts the new vertex into the
// ring too, so the next point can land on either half.
class RimAnchors
{
public:
    enum class Placement
    {
        Resolved,
        Failed, // the point was on the ring but the split could not be made
    };

    RimAnchors(BrushMesh& mesh, std::vector<std::uint32_t> ring, const BrushFaceFrame& frame,
               float tolerance)
        : Mesh(mesh), Ring(std::move(ring)), Frame(frame), Tolerance(tolerance)
    {
    }

    Placement Resolve(Vec2d point, std::uint32_t& outVertex)
    {
        for (const std::pair<Vec2d, std::uint32_t>& known : Known)
            if (Distance(known.first, point) <= Tolerance)
            {
                outVertex = known.second;
                return Placement::Resolved;
            }

        std::vector<Vec2d> ring;
        ring.reserve(Ring.size());
        for (std::uint32_t index : Ring)
            ring.push_back(Frame.ToFrame(Mesh.Vertices[index].Position));

        for (std::size_t i = 0; i < ring.size(); ++i)
            if (Distance(ring[i], point) <= Tolerance)
            {
                outVertex = Ring[i];
                Known.emplace_back(point, outVertex);
                return Placement::Resolved;
            }

        for (std::size_t i = 0; i < ring.size(); ++i)
        {
            const std::size_t next = (i + 1) % ring.size();
            if (DistanceToSegment2D(point, ring[i], ring[next]) > Tolerance)
                continue;
            const std::optional<BrushOps::BrushEdgeSplit> split = BrushOps::InsertVertexOnEdge(
                Mesh, Ring[i], Ring[next], Frame.ToWorld(point), Tolerance);
            if (!split.has_value())
                return Placement::Failed;
            Mesh = split->Mesh;
            outVertex = split->Vertex;
            Ring.insert(Ring.begin() + static_cast<std::ptrdiff_t>(i) + 1, outVertex);
            Known.emplace_back(point, outVertex);
            return Placement::Resolved;
        }

        outVertex = static_cast<std::uint32_t>(Mesh.Vertices.size());
        Mesh.Vertices.push_back(BrushVertex{ Frame.ToWorld(point) });
        Known.emplace_back(point, outVertex);
        return Placement::Resolved;
    }

private:
    BrushMesh& Mesh;
    std::vector<std::uint32_t> Ring;
    const BrushFaceFrame& Frame;
    float Tolerance;
    std::vector<std::pair<Vec2d, std::uint32_t>> Known;
};

// The 2D half of a carve: the outline snapped onto the host rim and the pieces
// the host breaks into around it. Separated from the mesh edit because the
// through-carve needs the same preparation on two faces before it touches
// either of them.
struct PreparedCarve
{
    CarveStatus Status = CarveStatus::Ok;
    std::vector<Vec2d> Outline;
    std::vector<std::vector<Vec2d>> Surround;
};

// Whether `outline` and `cell` bound the same region: each one's vertices all
// lie on the other's boundary, and the areas agree to within a sliver a snap
// tolerance wide around the cell's rim. The boundary tests alone would pass a
// shape that visits every corner but bulges between them; the area alone would
// pass a shape of the right size in the wrong place.
bool OutlineFillsCell(std::span<const Vec2d> outline, std::span<const Vec2d> cell)
{
    for (const Vec2d& point : outline)
        if (ClassifyPointInPolygon2D(cell, point, kCarveSnapTolerance) != PointPolygonRelation::Boundary)
            return false;
    for (const Vec2d& point : cell)
        if (ClassifyPointInPolygon2D(outline, point, kCarveSnapTolerance) != PointPolygonRelation::Boundary)
            return false;
    float perimeter = 0.0f;
    for (std::size_t i = 0; i < cell.size(); ++i)
        perimeter += Distance(cell[i], cell[(i + 1) % cell.size()]);
    return std::abs(std::abs(PolygonSignedArea(outline)) - std::abs(PolygonSignedArea(cell)))
        <= kCarveSnapTolerance * perimeter;
}

PreparedCarve PrepareCarve(const BrushFaceFrame& frame, std::span<const Vec2d> outline)
{
    PreparedCarve prepared;
    if (frame.Outline.size() < 3 || outline.size() < 3)
    {
        prepared.Status = CarveStatus::InvalidOutline;
        return prepared;
    }

    prepared.Outline.reserve(outline.size());
    for (const Vec2d& point : outline)
        prepared.Outline.push_back(SnapPointToPolygon2D(frame.Outline, point, kCarveSnapTolerance));

    const std::vector<Vec2d>& snapped = prepared.Outline;
    for (std::size_t i = 0; i < snapped.size(); ++i)
        if (Distance(snapped[i], snapped[(i + 1) % snapped.size()]) < kCarveSnapTolerance)
        {
            prepared.Status = CarveStatus::DegenerateEdge;
            return prepared;
        }
    if (!IsSimplePolygon2D(snapped, kCarveSnapTolerance) || PolygonSignedArea(snapped) <= 0.0f)
    {
        prepared.Status = CarveStatus::InvalidOutline;
        return prepared;
    }
    if (!PolygonContainsPolygon2D(frame.Outline, snapped, kCarveSnapTolerance))
    {
        prepared.Status = CarveStatus::OutsideHost;
        return prepared;
    }

    // A shape that is the face leaves no surround. That is not a carve for the
    // flat kernel, which says so, but a through-carve of a loop cell is exactly
    // this: the cell's whole rim walled into a tunnel.
    if (OutlineFillsCell(snapped, frame.Outline))
        return prepared;

    SurroundResult surround = SurroundPolygons(frame.Outline, snapped, kCarveSnapTolerance);
    if (surround.Status != CarveStatus::Ok)
    {
        prepared.Status = surround.Status;
        return prepared;
    }
    if (surround.Pieces.empty())
    {
        prepared.Status = CarveStatus::InvalidOutline;
        return prepared;
    }
    prepared.Surround = std::move(surround.Pieces);
    return prepared;
}

// Rewrites `face` into the opening plus its surround, minting and anchoring
// vertices as it goes. The opening takes the host's slot, so a face index the
// caller already holds keeps meaning the thing it selected.
CarveStatus ApplyCarve(BrushMesh& out, std::uint32_t face, const BrushFaceFrame& frame,
                       const PreparedCarve& prepared, std::uint32_t& outCutFace,
                       std::vector<std::uint32_t>& outSurroundFaces)
{
    RimAnchors anchors(out, out.Faces[face].Loop, frame, kCarveSnapTolerance);
    const auto toLoop = [&](const std::vector<Vec2d>& polygon,
                            std::vector<std::uint32_t>& loop) -> bool {
        loop.clear();
        loop.reserve(polygon.size());
        for (const Vec2d& point : polygon)
        {
            std::uint32_t vertex = 0;
            if (anchors.Resolve(point, vertex) != RimAnchors::Placement::Resolved)
                return false;
            loop.push_back(vertex);
        }
        return true;
    };

    std::vector<std::uint32_t> cutLoop;
    if (!toLoop(prepared.Outline, cutLoop))
        return CarveStatus::TopologyFailure;
    std::vector<std::vector<std::uint32_t>> surroundLoops;
    surroundLoops.reserve(prepared.Surround.size());
    for (const std::vector<Vec2d>& piece : prepared.Surround)
    {
        std::vector<std::uint32_t> loop;
        if (!toLoop(piece, loop))
            return CarveStatus::TopologyFailure;
        surroundLoops.push_back(std::move(loop));
    }

    const FaceMaterial material = out.Faces[face].Material;
    outCutFace = face;
    out.Faces[face].Loop = std::move(cutLoop);
    out.Faces[face].Normal = BrushComputeFaceNormal(out, out.Faces[face]);
    for (std::vector<std::uint32_t>& loop : surroundLoops)
    {
        BrushFace piece;
        piece.Loop = std::move(loop);
        piece.Material = material;
        piece.Normal = BrushComputeFaceNormal(out, piece);
        outSurroundFaces.push_back(static_cast<std::uint32_t>(out.Faces.size()));
        out.Faces.push_back(std::move(piece));
    }

    return CarveStatus::Ok;
}
}

CarveOutcome CarveFacePolygon(const BrushMesh& mesh, std::uint32_t face, const BrushFaceFrame& frame,
                              std::span<const Vec2d> outline)
{
    if (face >= mesh.Faces.size())
        return CarveOutcome::Failure(CarveStatus::TopologyFailure);

    const PreparedCarve prepared = PrepareCarve(frame, outline);
    if (prepared.Status != CarveStatus::Ok)
        return CarveOutcome::Failure(prepared.Status);
    if (prepared.Surround.empty())
        return CarveOutcome::Failure(CarveStatus::InvalidOutline); // the shape is the whole face

    CarveSuccess success;
    BrushMesh out = mesh;
    std::uint32_t cutFace = 0;
    const CarveStatus status =
        ApplyCarve(out, face, frame, prepared, cutFace, success.SurroundFaces);
    if (status != CarveStatus::Ok)
        return CarveOutcome::Failure(status);

    success.CutFaces.push_back(cutFace);
    success.Mesh = std::move(out);
    return CarveOutcome::Success(std::move(success));
}

namespace
{
// Below this the projection is singular: the direction lies in the target
// plane, so the map between the two faces collapses a dimension and the tunnel
// has no thickness to speak of. This is a numerical limit, not an angle
// preference -- any non-singular affine map between the planes preserves the
// outline's simplicity, so there is nothing else to require of the angle.
constexpr float kProjectionEpsilon = 1e-3f;

Vec3d Centroid(const std::vector<Vec3d>& points)
{
    Vec3d sum{ 0.0f, 0.0f, 0.0f };
    for (const Vec3d& p : points)
        sum = sum + p;
    return sum * (1.0f / static_cast<float>(points.size()));
}

struct ThroughTarget
{
    std::uint32_t Face = 0;
    BrushFaceFrame Frame;
    std::vector<Vec2d> Outline; // the projection in the target's frame, counter-clockwise
};

// The first face the carve comes out of: the nearest one the projection reaches
// from the front, which the whole outline still lands inside.
//
// `exclude` keeps the search off the host's own plane; faces there point the
// wrong way along the projection and are rejected by the sign test anyway, but
// saying so is cheaper than relying on it.
std::optional<ThroughTarget> FindOppositeFace(const BrushMesh& mesh, std::uint32_t host,
                                              Vec3d direction, const std::vector<Vec3d>& outline,
                                              float planarTol, CarveStatus& status)
{
    const Vec3d from = Centroid(outline);
    std::optional<std::uint32_t> nearest;
    std::optional<BrushFaceFrame> nearestFrame;
    float nearestDistance = 0.0f;
    float nearestFacing = 0.0f;

    for (std::uint32_t f = 0; f < mesh.Faces.size(); ++f)
    {
        if (f == host)
            continue;
        const BrushFaceFrameResult candidate = FaceFrame(mesh, f, planarTol);
        if (!candidate.Frame.has_value())
            continue;
        const float facing = candidate.Frame->Normal.Dot(direction);
        if (facing <= 0.0f)
            continue; // the carve would enter this face, not leave through it
        const float denominator = facing;
        if (std::abs(denominator) < 1e-6f)
            continue;
        const float distance =
            (candidate.Frame->Origin - from).Dot(candidate.Frame->Normal) / denominator;
        if (!(distance > kCarveSnapTolerance) || !std::isfinite(distance))
            continue;
        if (ClassifyPointInPolygon2D(candidate.Frame->Outline,
                                     candidate.Frame->ToFrame(from + direction * distance),
                                     kCarveSnapTolerance)
            == PointPolygonRelation::Outside)
            continue;
        if (nearest.has_value() && distance >= nearestDistance)
            continue;
        nearest = f;
        nearestFrame = candidate.Frame;
        nearestDistance = distance;
        nearestFacing = facing;
    }

    if (!nearest.has_value())
    {
        status = CarveStatus::NoOppositeFace;
        return std::nullopt;
    }
    if (nearestFacing < kProjectionEpsilon)
    {
        status = CarveStatus::InvalidProjection;
        return std::nullopt;
    }

    // The whole outline has to land on that face, not just its centre.
    std::vector<Vec2d> projected;
    projected.reserve(outline.size());
    for (const Vec3d& point : outline)
    {
        const float distance = (nearestFrame->Origin - point).Dot(nearestFrame->Normal) / nearestFacing;
        if (!(distance > kCarveSnapTolerance) || !std::isfinite(distance))
        {
            status = CarveStatus::NoOppositeFace;
            return std::nullopt;
        }
        projected.push_back(nearestFrame->ToFrame(point + direction * distance));
    }

    // The far face looks the other way, so the projection arrives wound
    // backwards; reversed, index k of the result is index n-1-k of the source.
    std::reverse(projected.begin(), projected.end());
    return ThroughTarget{ *nearest, std::move(*nearestFrame), std::move(projected) };
}

// Drops `remove` from the mesh and rewrites the indices the caller is holding.
void DropFaces(BrushMesh& mesh, const std::vector<std::uint32_t>& remove,
               std::vector<std::vector<std::uint32_t>*> rewrite)
{
    std::vector<bool> dropped(mesh.Faces.size(), false);
    for (std::uint32_t index : remove)
        if (index < dropped.size())
            dropped[index] = true;

    std::vector<std::uint32_t> moved(mesh.Faces.size(), 0xFFFFFFFFu);
    std::vector<BrushFace> kept;
    kept.reserve(mesh.Faces.size());
    for (std::uint32_t i = 0; i < mesh.Faces.size(); ++i)
    {
        if (dropped[i])
            continue;
        moved[i] = static_cast<std::uint32_t>(kept.size());
        kept.push_back(std::move(mesh.Faces[i]));
    }
    mesh.Faces = std::move(kept);

    for (std::vector<std::uint32_t>* indices : rewrite)
        for (std::uint32_t& index : *indices)
            index = moved[index];
}

// Drops vertices no face uses any more, which a carve that removes a whole
// corner of a brush leaves behind. The kernel owes a mesh that needs no repair,
// and an unreferenced vertex is exactly the kind of thing repair would
// otherwise quietly fix.
void CompactVertices(BrushMesh& mesh)
{
    std::vector<bool> used(mesh.Vertices.size(), false);
    for (const BrushFace& face : mesh.Faces)
        for (std::uint32_t index : face.Loop)
            if (index < used.size())
                used[index] = true;

    std::vector<std::uint32_t> moved(mesh.Vertices.size(), 0xFFFFFFFFu);
    std::vector<BrushVertex> kept;
    kept.reserve(mesh.Vertices.size());
    for (std::uint32_t i = 0; i < mesh.Vertices.size(); ++i)
    {
        if (!used[i])
            continue;
        moved[i] = static_cast<std::uint32_t>(kept.size());
        kept.push_back(mesh.Vertices[i]);
    }
    if (kept.size() == mesh.Vertices.size())
        return;

    mesh.Vertices = std::move(kept);
    for (BrushFace& face : mesh.Faces)
        for (std::uint32_t& index : face.Loop)
            index = moved[index];

    std::vector<std::array<std::uint32_t, 2>> edges;
    edges.reserve(mesh.SoftEdges.size());
    for (const std::array<std::uint32_t, 2>& edge : mesh.SoftEdges)
        if (moved[edge[0]] != 0xFFFFFFFFu && moved[edge[1]] != 0xFFFFFFFFu)
            edges.push_back(BrushSoftEdgeKey(moved[edge[0]], moved[edge[1]]));
    mesh.SoftEdges = std::move(edges);
}

// A tunnel wall spanning one outline edge, wound so its normal points into the
// tunnel rather than into the solid.
//
// The wall's axes would be edge-on to the cap's texture projection, so they are
// re-derived from the wall normal while keeping the authored scale, offset and
// rotation -- the same rule the extrude and bridge paths follow.
BrushFace TunnelWall(const BrushMesh& mesh, std::uint32_t a, std::uint32_t b, std::uint32_t bMate,
                     std::uint32_t aMate, Vec3d towardTunnel, const FaceMaterial& source)
{
    BrushFace wall;
    wall.Loop = { a, b, bMate, aMate };
    wall.Normal = BrushComputeFaceNormal(mesh, wall);
    if (wall.Normal.Dot(towardTunnel) < 0.0f)
    {
        std::reverse(wall.Loop.begin(), wall.Loop.end());
        wall.Normal = BrushComputeFaceNormal(mesh, wall);
    }
    wall.Material = source;
    wall.Material.Uv = UvProjectionForNormal(wall.Normal, source.Uv.WorldAligned);
    wall.Material.Uv.Scale = source.Uv.Scale;
    wall.Material.Uv.Offset = source.Uv.Offset;
    wall.Material.Uv.Rotation = source.Uv.Rotation;
    return wall;
}

// Whether the four corners of a wall still share a plane. They do by
// construction under a fixed projection direction; snapping the projected
// outline onto the far face's rim is what can bend one.
bool WallIsPlanar(const BrushMesh& mesh, const BrushFace& wall)
{
    const Vec3d normal = BrushComputeFaceNormal(mesh, wall);
    if (normal.SqrMagnitude() < 0.5f)
        return false;
    const Vec3d origin = mesh.Vertices[wall.Loop.front()].Position;
    float extent = 0.0f;
    for (std::uint32_t index : wall.Loop)
        extent = std::max(extent, (mesh.Vertices[index].Position - origin).Magnitude());
    const float budget = std::max(kCarveSnapTolerance, 1e-3f * extent);
    for (std::uint32_t index : wall.Loop)
        if (std::abs((mesh.Vertices[index].Position - origin).Dot(normal)) > budget)
            return false;
    return true;
}

// The faces sharing a plane with `seed`, which is how a channel crossing a side
// that an earlier carve already subdivided finds the whole side rather than the
// one face it happened to enter through.
std::vector<std::uint32_t> CoplanarFaces(const BrushMesh& mesh, const BrushFaceFrame& seed,
                                         float tolerance)
{
    std::vector<std::uint32_t> faces;
    for (std::uint32_t f = 0; f < mesh.Faces.size(); ++f)
    {
        const BrushFace& candidate = mesh.Faces[f];
        if (candidate.Loop.size() < 3)
            continue;
        const Vec3d normal = BrushComputeFaceNormal(mesh, candidate);
        if (normal.Dot(seed.Normal) < 0.999f)
            continue;
        bool onPlane = true;
        for (std::uint32_t index : candidate.Loop)
            if (std::abs((mesh.Vertices[index].Position - seed.Origin).Dot(seed.Normal)) > tolerance)
            {
                onPlane = false;
                break;
            }
        if (onPlane)
            faces.push_back(f);
    }
    return faces;
}

// The outline of a set of faces, found by counting: a directed edge whose
// reverse is not also in the set is on the boundary. No geometry is
// intersected, so merging coplanar faces costs nothing in robustness.
//
// nullopt when the boundary is not a single loop, which means the merged
// surface has a hole or falls into separate pieces. A channel across that is
// refused rather than guessed at.
std::optional<std::vector<std::uint32_t>> UnionBoundary(const BrushMesh& mesh,
                                                        const std::vector<std::uint32_t>& faces)
{
    std::vector<std::pair<std::uint32_t, std::uint32_t>> directed;
    for (std::uint32_t f : faces)
    {
        const std::vector<std::uint32_t>& loop = mesh.Faces[f].Loop;
        for (std::size_t i = 0; i < loop.size(); ++i)
            directed.emplace_back(loop[i], loop[(i + 1) % loop.size()]);
    }

    std::vector<std::pair<std::uint32_t, std::uint32_t>> boundary;
    for (const std::pair<std::uint32_t, std::uint32_t>& edge : directed)
    {
        bool paired = false;
        for (const std::pair<std::uint32_t, std::uint32_t>& other : directed)
            if (other.first == edge.second && other.second == edge.first)
            {
                paired = true;
                break;
            }
        if (!paired)
            boundary.push_back(edge);
    }
    if (boundary.size() < 3)
        return std::nullopt;

    std::vector<std::uint32_t> loop;
    loop.push_back(boundary.front().first);
    std::uint32_t at = boundary.front().second;
    std::vector<bool> used(boundary.size(), false);
    used[0] = true;
    while (at != loop.front())
    {
        bool advanced = false;
        for (std::size_t i = 0; i < boundary.size(); ++i)
        {
            if (used[i] || boundary[i].first != at)
                continue;
            used[i] = true;
            loop.push_back(at);
            at = boundary[i].second;
            advanced = true;
            break;
        }
        if (!advanced)
            return std::nullopt; // the boundary is not a loop
    }
    for (bool consumed : used)
        if (!consumed)
            return std::nullopt; // more than one loop: a hole, or separate pieces
    return loop;
}

// Whether the faces form one connected body. A channel that cuts a brush into
// two pieces leaves a mesh that is still closed and still valid, and is still
// not what anyone asked a carve to do.
bool MeshIsConnected(const BrushMesh& mesh)
{
    if (mesh.Faces.empty())
        return true;
    std::vector<bool> reached(mesh.Faces.size(), false);
    std::vector<std::uint32_t> stack{ 0 };
    reached[0] = true;
    std::size_t count = 1;
    const auto sharesEdge = [&mesh](std::uint32_t a, std::uint32_t b) {
        for (std::uint32_t v : mesh.Faces[a].Loop)
            for (std::uint32_t w : mesh.Faces[b].Loop)
                if (v == w)
                    return true;
        return false;
    };
    while (!stack.empty())
    {
        const std::uint32_t face = stack.back();
        stack.pop_back();
        for (std::uint32_t f = 0; f < mesh.Faces.size(); ++f)
        {
            if (reached[f] || !sharesEdge(face, f))
                continue;
            reached[f] = true;
            ++count;
            stack.push_back(f);
        }
    }
    return count == mesh.Faces.size();
}

// The face on the other side of a host-rim edge: the one the channel has to
// open through, and the plane that decides how far a flush run extends.
std::optional<std::uint32_t> FaceAcross(const BrushMesh& mesh, std::uint32_t a, std::uint32_t b,
                                        const std::vector<std::uint32_t>& skip)
{
    for (std::uint32_t f = 0; f < mesh.Faces.size(); ++f)
    {
        if (std::find(skip.begin(), skip.end(), f) != skip.end())
            continue;
        const std::vector<std::uint32_t>& loop = mesh.Faces[f].Loop;
        for (std::size_t i = 0; i < loop.size(); ++i)
        {
            const std::uint32_t from = loop[i];
            const std::uint32_t to = loop[(i + 1) % loop.size()];
            if ((from == a && to == b) || (from == b && to == a))
                return f;
        }
    }
    return std::nullopt;
}

// Cuts one channel out of the side of the brush it runs through, which is what
// opens a flush carve into a notch rather than walling it off.
//
// The channel's own corners are already vertices of that side, because the two
// carves anchored them onto its shared edges. What it may still need is the
// rest of the side: when an earlier carve subdivided it, the channel crosses
// several coplanar faces, and they are merged into one boundary first.
CarveStatus OpenNotch(BrushMesh& out, const std::vector<std::uint32_t>& channel,
                      std::uint32_t hostCut, std::uint32_t farCut, float planarTol,
                      std::vector<std::uint32_t>& retired, CarveSuccess& success)
{
    if (channel.size() < 4)
        return CarveStatus::TopologyFailure;

    std::vector<std::uint32_t> skip = retired;
    skip.push_back(hostCut);
    skip.push_back(farCut);
    const std::optional<std::uint32_t> side = FaceAcross(out, channel[0], channel[1], skip);
    if (!side.has_value())
        return CarveStatus::TopologyFailure;

    const BrushFaceFrameResult sideFrame = FaceFrame(out, *side, planarTol);
    if (!sideFrame.Frame.has_value())
        return CarveStatus::NonPlanarTunnelWall;

    // The notch only exists if the channel lies in that side's plane, which it
    // does exactly when the side is not slanted away from the projection.
    float extent = 0.0f;
    for (std::uint32_t index : channel)
        extent = std::max(extent, (out.Vertices[index].Position - sideFrame.Frame->Origin).Magnitude());
    const float budget = std::max(kCarveSnapTolerance, planarTol * extent);
    for (std::uint32_t index : channel)
        if (std::abs((out.Vertices[index].Position - sideFrame.Frame->Origin)
                         .Dot(sideFrame.Frame->Normal))
            > budget)
            return CarveStatus::NonPlanarTunnelWall;

    std::vector<Vec2d> channel2D;
    channel2D.reserve(channel.size());
    for (std::uint32_t index : channel)
        channel2D.push_back(sideFrame.Frame->ToFrame(out.Vertices[index].Position));
    if (PolygonSignedArea(channel2D) < 0.0f)
        std::reverse(channel2D.begin(), channel2D.end());

    std::vector<std::uint32_t> merged{ *side };
    std::vector<std::uint32_t> ring = out.Faces[*side].Loop;
    if (!PolygonContainsPolygon2D(sideFrame.Frame->Outline, channel2D, kCarveSnapTolerance))
    {
        merged = CoplanarFaces(out, *sideFrame.Frame, budget);
        const std::optional<std::vector<std::uint32_t>> boundary = UnionBoundary(out, merged);
        if (!boundary.has_value())
            return CarveStatus::ChannelCrossesHole;
        ring = *boundary;
    }

    std::vector<Vec2d> ring2D;
    ring2D.reserve(ring.size());
    for (std::uint32_t index : ring)
        ring2D.push_back(sideFrame.Frame->ToFrame(out.Vertices[index].Position));
    if (PolygonSignedArea(ring2D) <= 0.0f)
        return CarveStatus::TopologyFailure;
    if (!PolygonContainsPolygon2D(ring2D, channel2D, kCarveSnapTolerance))
        return CarveStatus::ChannelCrossesHole;

    // The channel can take the whole side, and that is a result rather than a
    // failure: it is what happens when a doorway is carved flush against an
    // existing one and the jamb between them goes away. Nothing is left to add,
    // and the closedness check at the end is what catches a channel that really
    // does open the solid.
    const float ringArea = PolygonSignedArea(ring2D);
    if (std::abs(ringArea - PolygonSignedArea(channel2D))
        <= std::max(kCarveSnapTolerance, 1e-3f * ringArea))
    {
        retired.insert(retired.end(), merged.begin(), merged.end());
        return CarveStatus::Ok;
    }

    const SurroundResult surround = SurroundPolygons(ring2D, channel2D, kCarveSnapTolerance);
    if (surround.Status != CarveStatus::Ok)
        return surround.Status;
    // Merged faces lose their individual materials to the one the channel
    // entered through; the pieces are one surface now, so they get one material.
    const FaceMaterial material = out.Faces[*side].Material;
    RimAnchors anchors(out, ring, *sideFrame.Frame, kCarveSnapTolerance);
    std::vector<BrushFace> pieces;
    pieces.reserve(surround.Pieces.size());
    for (const std::vector<Vec2d>& piece : surround.Pieces)
    {
        BrushFace built;
        built.Loop.reserve(piece.size());
        for (const Vec2d& point : piece)
        {
            std::uint32_t vertex = 0;
            if (anchors.Resolve(point, vertex) != RimAnchors::Placement::Resolved)
                return CarveStatus::TopologyFailure;
            built.Loop.push_back(vertex);
        }
        built.Material = material;
        pieces.push_back(std::move(built));
    }
    for (BrushFace& piece : pieces)
    {
        piece.Normal = BrushComputeFaceNormal(out, piece);
        success.SurroundFaces.push_back(static_cast<std::uint32_t>(out.Faces.size()));
        out.Faces.push_back(std::move(piece));
    }
    retired.insert(retired.end(), merged.begin(), merged.end());
    return CarveStatus::Ok;
}

bool MeshIsClosed(const BrushMesh& mesh)
{
    std::vector<std::pair<std::uint64_t, int>> counts;
    const auto bump = [&counts](std::uint32_t a, std::uint32_t b) {
        const std::uint64_t key = a < b ? (static_cast<std::uint64_t>(a) << 32) | b
                                        : (static_cast<std::uint64_t>(b) << 32) | a;
        for (std::pair<std::uint64_t, int>& entry : counts)
            if (entry.first == key)
            {
                ++entry.second;
                return;
            }
        counts.emplace_back(key, 1);
    };
    for (const BrushFace& face : mesh.Faces)
        for (std::size_t i = 0; i < face.Loop.size(); ++i)
            bump(face.Loop[i], face.Loop[(i + 1) % face.Loop.size()]);
    for (const std::pair<std::uint64_t, int>& entry : counts)
        if (entry.second != 2)
            return false;
    return true;
}
}

CarveOutcome CarveFacePolygonThrough(const BrushMesh& mesh, std::uint32_t face,
                                     const BrushFaceFrame& frame, std::span<const Vec2d> outline,
                                     float planarTol)
{
    if (face >= mesh.Faces.size())
        return CarveOutcome::Failure(CarveStatus::TopologyFailure);

    const PreparedCarve prepared = PrepareCarve(frame, outline);
    if (prepared.Status != CarveStatus::Ok)
        return CarveOutcome::Failure(prepared.Status);

    const bool wasClosed = MeshIsClosed(mesh);
    CarveSuccess success;
    BrushMesh out = mesh;
    std::uint32_t cutFace = 0;
    CarveStatus status = ApplyCarve(out, face, frame, prepared, cutFace, success.SurroundFaces);
    if (status != CarveStatus::Ok)
        return CarveOutcome::Failure(status);

    const std::vector<std::uint32_t> loopHost = out.Faces[cutFace].Loop;
    std::vector<Vec3d> points;
    points.reserve(loopHost.size());
    for (std::uint32_t index : loopHost)
        points.push_back(out.Vertices[index].Position);

    const Vec3d direction = frame.Normal * -1.0f;
    CarveStatus search = CarveStatus::NoOppositeFace;
    const std::optional<ThroughTarget> target =
        FindOppositeFace(out, cutFace, direction, points, planarTol, search);
    if (!target.has_value())
    {
        // An open host -- a plane -- has nothing opposite, and there the pierce
        // is just a hole. A closed solid keeps the refusal: a blind hole would
        // silently open the solid.
        if (search != CarveStatus::NoOppositeFace || wasClosed)
            return CarveOutcome::Failure(search);
        DropFaces(out, { cutFace }, { &success.SurroundFaces });
        CompactVertices(out);
        success.Mesh = std::move(out);
        return CarveOutcome::Success(std::move(success));
    }

    const PreparedCarve far = PrepareCarve(target->Frame, target->Outline);
    if (far.Status != CarveStatus::Ok)
        return CarveOutcome::Failure(far.Status);

    std::uint32_t farCutFace = 0;
    std::vector<std::uint32_t> farSurround;
    status = ApplyCarve(out, target->Face, target->Frame, far, farCutFace, farSurround);
    if (status != CarveStatus::Ok)
        return CarveOutcome::Failure(status);
    success.SurroundFaces.insert(success.SurroundFaces.end(), farSurround.begin(), farSurround.end());

    const std::vector<std::uint32_t> loopFar = out.Faces[farCutFace].Loop;
    if (loopFar.size() != loopHost.size())
        return CarveOutcome::Failure(CarveStatus::TopologyFailure);
    const std::size_t count = loopHost.size();
    const auto mate = [&](std::size_t i) { return loopFar[count - 1 - i]; };

    // An outline edge that runs along the host rim has a face on the other side
    // of it. The tunnel does not wall there: it opens a notch through that face
    // instead, which is what makes a doorway a doorway rather than a window
    // with its sill still in place.
    std::vector<bool> flush(count, false);
    for (std::size_t i = 0; i < count; ++i)
    {
        const Vec2d a = prepared.Outline[i];
        const Vec2d b = prepared.Outline[(i + 1) % count];
        const Vec2d middle{ (a.X + b.X) * 0.5f, (a.Y + b.Y) * 0.5f };
        flush[i] = ClassifyPointInPolygon2D(frame.Outline, a, kCarveSnapTolerance)
                       == PointPolygonRelation::Boundary
                && ClassifyPointInPolygon2D(frame.Outline, b, kCarveSnapTolerance)
                       == PointPolygonRelation::Boundary
                && ClassifyPointInPolygon2D(frame.Outline, middle, kCarveSnapTolerance)
                       == PointPolygonRelation::Boundary;
    }

    // A flush edge whose face across is coplanar with the host is a seam -- an
    // earlier loop cut, say -- and the surface continues past it. A seam is
    // walled like an interior edge; only a bent rim opens a notch. The far face
    // decides the same for its own edge, and the two must agree: a seam on one
    // end and a rim on the other would leave a wall standing on a strip that is
    // being cut away, and closing that needs a volume boolean this is not.
    //
    // For the edges that do open a notch, which plane each opens through, so a
    // run stops where the plane changes. Two flush edges can be consecutive and
    // still belong to different sides -- a carve flush into a corner, or one
    // sharing the jamb of an earlier doorway -- and merging those into one
    // channel would ask for a channel that bends.
    std::vector<bool> opensNotch(count, false);
    std::vector<Vec3d> sideNormal(count, Vec3d{ 0.0f, 0.0f, 0.0f });
    std::vector<float> sideOffset(count, 0.0f);
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!flush[i])
            continue;
        const std::size_t next = (i + 1) % count;
        const std::optional<std::uint32_t> across =
            FaceAcross(out, loopHost[i], loopHost[next], { cutFace, farCutFace });
        const std::optional<std::uint32_t> farAcross =
            FaceAcross(out, mate(next), mate(i), { cutFace, farCutFace });
        if (!across.has_value() || !farAcross.has_value())
            return CarveOutcome::Failure(CarveStatus::TopologyFailure);
        const bool seam = BrushFacesCoplanar(out, cutFace, *across);
        const bool farSeam = BrushFacesCoplanar(out, farCutFace, *farAcross);
        if (seam != farSeam)
            return CarveOutcome::Failure(CarveStatus::ChannelCrossesHole);
        if (seam)
            continue;
        opensNotch[i] = true;
        sideNormal[i] = BrushComputeFaceNormal(out, out.Faces[*across]);
        sideOffset[i] = sideNormal[i].Dot(out.Vertices[out.Faces[*across].Loop.front()].Position);
    }
    const auto samePlane = [&](std::size_t a, std::size_t b) {
        return sideNormal[a].Dot(sideNormal[b]) > 0.999f
            && std::abs(sideOffset[a] - sideOffset[b]) <= kCarveSnapTolerance * 10.0f;
    };

    std::size_t firstOpen = count;
    for (std::size_t i = 0; i < count; ++i)
        if (!opensNotch[i])
        {
            firstOpen = i;
            break;
        }
    if (firstOpen == count)
        return CarveOutcome::Failure(CarveStatus::InvalidOutline); // the outline is the whole rim

    const FaceMaterial material = mesh.Faces[face].Material;
    std::vector<std::uint32_t> retired{ cutFace, farCutFace };
    std::size_t at = 0;
    while (at < count)
    {
        const std::size_t i = (firstOpen + at) % count;
        if (!opensNotch[i])
        {
            const std::size_t next = (i + 1) % count;
            // The outline is counter-clockwise, so the tunnel is to the left of
            // every edge; that is the side the wall has to face.
            const Vec2d a = prepared.Outline[i];
            const Vec2d b = prepared.Outline[next];
            const Vec3d towardTunnel = frame.AxisU * -(b.Y - a.Y) + frame.AxisV * (b.X - a.X);

            BrushFace wall = TunnelWall(out, loopHost[i], loopHost[next], mate(next), mate(i),
                                        towardTunnel, material);
            if (!WallIsPlanar(out, wall))
                return CarveOutcome::Failure(CarveStatus::NonPlanarTunnelWall);
            success.TunnelWalls.push_back(static_cast<std::uint32_t>(out.Faces.size()));
            out.Faces.push_back(std::move(wall));
            ++at;
            continue;
        }

        // One run of flush edges: they share a plane and are cut out of it in
        // one piece, since a channel crossing two faces of the same side is one
        // channel.
        std::size_t length = 0;
        while (length < count && opensNotch[(firstOpen + at + length) % count]
               && samePlane((firstOpen + at + length) % count, i))
            ++length;

        // The run's channel: out along the host rim, back along the far one.
        const auto vertexAt = [&](std::size_t k) { return (firstOpen + at + k) % count; };
        std::vector<std::uint32_t> channelLoop;
        channelLoop.reserve((length + 1) * 2);
        for (std::size_t k = 0; k <= length; ++k)
            channelLoop.push_back(loopHost[vertexAt(k)]);
        for (std::size_t k = length + 1; k-- > 0;)
            channelLoop.push_back(mate(vertexAt(k)));

        const CarveStatus notched =
            OpenNotch(out, channelLoop, cutFace, farCutFace, planarTol, retired, success);
        if (notched != CarveStatus::Ok)
            return CarveOutcome::Failure(notched);
        at += length;
    }

    DropFaces(out, retired, { &success.SurroundFaces, &success.TunnelWalls });
    CompactVertices(out);
    // A carve must not open or dismember the solid it went through. Both are
    // possible outcomes of a channel that reaches too far -- a slot flush on
    // two opposite sides halves the brush -- and neither is a carve.
    if (!MeshIsConnected(out) || (wasClosed && !MeshIsClosed(out)))
        return CarveOutcome::Failure(CarveStatus::TopologyFailure);
    success.Mesh = std::move(out);
    return CarveOutcome::Success(std::move(success));
}

namespace
{
// The face of `mesh` with these corner positions in this cyclic order, if any.
// Other vertices are allowed only where they lie on the segment between two
// consecutive corners: a neighbour's carve splits a shared edge, and the face
// on the far side of it is still that face.
std::optional<std::uint32_t> FindFaceByCorners(const BrushMesh& mesh, const std::vector<Vec3d>& corners)
{
    const float tol2 = kCarveSnapTolerance * kCarveSnapTolerance;
    const std::size_t m = corners.size();
    const auto same = [&](Vec3d a, Vec3d b) { return (a - b).SqrMagnitude() <= tol2; };
    const auto onSegment = [&](Vec3d p, Vec3d a, Vec3d b) {
        const Vec3d ab = b - a;
        const float length2 = ab.SqrMagnitude();
        if (length2 <= 0.0f)
            return same(p, a);
        const float t = std::clamp((p - a).Dot(ab) / length2, 0.0f, 1.0f);
        return (p - (a + ab * t)).SqrMagnitude() <= tol2;
    };
    for (std::uint32_t f = 0; f < mesh.Faces.size(); ++f)
    {
        const std::vector<std::uint32_t>& loop = mesh.Faces[f].Loop;
        const std::size_t n = loop.size();
        if (n < m)
            continue;
        for (std::size_t start = 0; start < n; ++start)
        {
            if (!same(mesh.Vertices[loop[start]].Position, corners[0]))
                continue;
            std::size_t expected = 1;
            bool ok = true;
            for (std::size_t j = 1; j < n && ok; ++j)
            {
                const Vec3d p = mesh.Vertices[loop[(start + j) % n]].Position;
                if (expected < m && same(p, corners[expected]))
                    ++expected;
                else
                    ok = onSegment(p, corners[expected - 1], corners[expected % m]);
            }
            if (ok && expected == m)
                return f;
        }
    }
    return std::nullopt;
}

// Carry face indices recorded against `before` over to `after`, which a later
// kernel call compacted. A face the later call removed drops out.
void CarryFaces(const BrushMesh& before, const BrushMesh& after, std::vector<std::uint32_t>& faces)
{
    std::vector<std::uint32_t> kept;
    kept.reserve(faces.size());
    for (std::uint32_t f : faces)
    {
        std::vector<Vec3d> corners;
        for (std::uint32_t v : before.Faces[f].Loop)
            corners.push_back(before.Vertices[v].Position);
        if (const std::optional<std::uint32_t> found = FindFaceByCorners(after, corners))
            kept.push_back(*found);
    }
    faces = std::move(kept);
}

Vec2d QuadUv(const BrushOps::BrushRectFaceFrame& quad, Vec3d world)
{
    const Vec3d rel = world - quad.Origin;
    return Vec2d{ rel.Dot(quad.AxisU), rel.Dot(quad.AxisV) };
}
}

FaceCorners CornersOf(const BrushMesh& mesh, std::uint32_t face)
{
    FaceCorners corners;
    corners.Corners.reserve(mesh.Faces[face].Loop.size());
    for (std::uint32_t v : mesh.Faces[face].Loop)
        corners.Corners.push_back(mesh.Vertices[v].Position);
    return corners;
}

namespace
{
bool SameFrame(const BrushFaceFrame& a, const BrushFaceFrame& b)
{
    return a.Origin == b.Origin && a.AxisU == b.AxisU && a.AxisV == b.AxisV;
}

float Perimeter(std::span<const Vec2d> polygon)
{
    float length = 0.0f;
    for (std::size_t i = 0; i < polygon.size(); ++i)
        length += Distance(polygon[i], polygon[(i + 1) % polygon.size()]);
    return length;
}
}

std::vector<std::uint32_t> CoplanarSurface(const BrushMesh& mesh, std::uint32_t face, float planarTol)
{
    std::vector<std::uint32_t> surface;
    if (face >= mesh.Faces.size())
        return surface;
    const Vec3d normal = BrushComputeFaceNormal(mesh, mesh.Faces[face]);
    const Vec3d origin = mesh.Vertices[mesh.Faces[face].Loop.front()].Position;
    const auto continues = [&](std::uint32_t candidate) {
        if (BrushComputeFaceNormal(mesh, mesh.Faces[candidate]).Dot(normal) <= 0.99f)
            return false;
        for (std::uint32_t v : mesh.Faces[candidate].Loop)
            if (std::abs((mesh.Vertices[v].Position - origin).Dot(normal)) > planarTol)
                return false;
        return true;
    };
    const auto sharesEdge = [&](std::uint32_t a, std::uint32_t b) {
        const std::vector<std::uint32_t>& la = mesh.Faces[a].Loop;
        const std::vector<std::uint32_t>& lb = mesh.Faces[b].Loop;
        for (std::size_t i = 0; i < la.size(); ++i)
            for (std::size_t j = 0; j < lb.size(); ++j)
            {
                const std::uint32_t a0 = la[i], a1 = la[(i + 1) % la.size()];
                const std::uint32_t b0 = lb[j], b1 = lb[(j + 1) % lb.size()];
                if ((a0 == b0 && a1 == b1) || (a0 == b1 && a1 == b0))
                    return true;
            }
        return false;
    };

    std::vector<bool> visited(mesh.Faces.size(), false);
    surface.push_back(face);
    visited[face] = true;
    for (std::size_t at = 0; at < surface.size(); ++at)
        for (std::uint32_t candidate = 0; candidate < mesh.Faces.size(); ++candidate)
            if (!visited[candidate] && sharesEdge(surface[at], candidate) && continues(candidate))
            {
                visited[candidate] = true;
                surface.push_back(candidate);
            }
    return surface;
}

CarveOutcome CarveAcrossSurface(const BrushMesh& mesh, std::span<const FaceCorners> faces,
                                const BrushFaceFrame& frame, std::span<const Vec2d> outline,
                                bool pierce, float planarTol)
{
    if (outline.size() < 3 || !IsSimplePolygon2D(outline, kCarveSnapTolerance)
        || PolygonSignedArea(outline) <= 0.0f)
        return CarveOutcome::Failure(CarveStatus::InvalidOutline);

    // Convexity is a requirement of distributed clipping, not of being a carve
    // host: the plain kernel carves into a concave face, and a carve that one
    // face owns whole goes to it directly. Only a carve no face owns is
    // distributed, and only then are the faces it is clipped to held to that.
    //
    // Reach is shared interior. A face the outline merely touches along its
    // rim is not reached: it needs no piece, so it is neither clipped to nor
    // judged for convexity.
    struct Reach
    {
        const FaceCorners* Face;
        std::vector<Vec2d> Region;
        std::vector<Vec2d> Piece;
    };
    std::vector<Reach> reached;
    for (const FaceCorners& face : faces)
    {
        Reach reach{ &face, {}, {} };
        reach.Region.reserve(face.Corners.size());
        for (const Vec3d& corner : face.Corners)
            reach.Region.push_back(frame.ToFrame(corner));
        if (PolygonsOverlap2D(outline, reach.Region, kCarveSnapTolerance))
            reached.push_back(std::move(reach));
    }

    // Direct single-face ownership: one reached face containing the whole
    // outline, decided by polygon containment alone. An outline on a seam
    // reaches two faces and is owned by one. Containment is the coverage proof
    // for this path; nothing about extents, and nothing about how many faces
    // were reached, enters into it.
    for (const Reach& reach : reached)
    {
        if (!PolygonContainsPolygon2D(reach.Region, outline, kCarveSnapTolerance))
            continue;
        const std::optional<std::uint32_t> face = FindFaceByCorners(mesh, reach.Face->Corners);
        if (!face.has_value())
            return CarveOutcome::Failure(CarveStatus::TopologyFailure);
        const BrushFaceFrameResult own = FaceFrame(mesh, *face, planarTol);
        if (!own.Frame.has_value())
            return CarveOutcome::Failure(own.Status);
        std::vector<Vec2d> carried;
        if (!SameFrame(*own.Frame, frame))
        {
            carried.reserve(outline.size());
            for (const Vec2d& point : outline)
                carried.push_back(own.Frame->ToFrame(frame.ToWorld(point)));
        }
        const std::span<const Vec2d> local = carried.empty() ? outline : std::span<const Vec2d>(carried);
        if (OutlineFillsCell(local, own.Frame->Outline))
        {
            if (!pierce)
            {
                CarveSuccess success;
                success.Mesh = mesh;
                success.CutFaces.push_back(*face); // the face is the shape, as it stands
                return CarveOutcome::Success(std::move(success));
            }
            return CarveFacePolygonThrough(mesh, *face, *own.Frame, own.Frame->Outline, planarTol);
        }
        return pierce ? CarveFacePolygonThrough(mesh, *face, *own.Frame, local, planarTol)
                      : CarveFacePolygon(mesh, *face, *own.Frame, local);
    }

    // Distributed: every reached face is a clipping host. A piece is the
    // intersection of the outline with the face, which half-plane clipping
    // computes when either one is convex: a convex face clips the outline, a
    // concave face is clipped by a convex outline. What a concave face cannot
    // yield is a piece in two parts -- the outline spanning across a hole an
    // earlier carve left -- and that shows as a piece that is not simple. The
    // pieces must then add up to the outline, since the faces are a planar
    // partition; anything short lies over a gap in the surface.
    const bool outlineConvex = IsConvexPolygon2D(outline, kCarveSnapTolerance);
    float covered = 0.0f;
    for (Reach& reach : reached)
    {
        if (IsConvexPolygon2D(reach.Region, kCarveSnapTolerance))
            reach.Piece = ClipPolygonToConvex2D(outline, reach.Region, kCarveSnapTolerance);
        else if (outlineConvex)
        {
            reach.Piece = ClipPolygonToConvex2D(reach.Region, outline, kCarveSnapTolerance);
            if (!reach.Piece.empty() && !IsSimplePolygon2D(reach.Piece, kCarveSnapTolerance))
                return CarveOutcome::Failure(CarveStatus::ChannelCrossesHole);
        }
        else
            return CarveOutcome::Failure(CarveStatus::HostNotConvex);
        covered += std::abs(PolygonSignedArea(reach.Piece));
    }
    if (std::abs(covered - PolygonSignedArea(outline)) > kCarveSnapTolerance * Perimeter(outline))
        return CarveOutcome::Failure(CarveStatus::OutsideHost);

    BrushMesh out = mesh;
    CarveSuccess success;
    for (const Reach& reach : reached)
    {
        if (reach.Piece.empty()
            || std::abs(PolygonSignedArea(reach.Piece)) <= kCarveSnapTolerance * Perimeter(reach.Region))
            continue; // numerical residue of a near-contact

        const std::optional<std::uint32_t> face = FindFaceByCorners(out, reach.Face->Corners);
        if (!face.has_value())
            return CarveOutcome::Failure(CarveStatus::TopologyFailure);
        const BrushFaceFrameResult own = FaceFrame(out, *face, planarTol);
        if (!own.Frame.has_value())
            return CarveOutcome::Failure(own.Status);
        std::vector<Vec2d> piece;
        piece.reserve(reach.Piece.size());
        for (const Vec2d& point : reach.Piece)
            piece.push_back(own.Frame->ToFrame(frame.ToWorld(point)));

        const bool fills = OutlineFillsCell(piece, own.Frame->Outline);
        if (fills && !pierce)
        {
            success.CutFaces.push_back(*face); // the face is the shape here, as it stands
            continue;
        }
        CarveOutcome step = pierce
            ? CarveFacePolygonThrough(out, *face, *own.Frame,
                                      fills ? std::span<const Vec2d>(own.Frame->Outline) : piece, planarTol)
            : CarveFacePolygon(out, *face, *own.Frame, piece);
        if (!step.Ok())
            return CarveOutcome::Failure(step.Status());
        CarveSuccess carved = step.Take();
        CarryFaces(out, carved.Mesh, success.CutFaces);
        CarryFaces(out, carved.Mesh, success.SurroundFaces);
        CarryFaces(out, carved.Mesh, success.TunnelWalls);
        success.CutFaces.insert(success.CutFaces.end(), carved.CutFaces.begin(), carved.CutFaces.end());
        success.SurroundFaces.insert(success.SurroundFaces.end(), carved.SurroundFaces.begin(),
                                     carved.SurroundFaces.end());
        success.TunnelWalls.insert(success.TunnelWalls.end(), carved.TunnelWalls.begin(),
                                   carved.TunnelWalls.end());
        out = std::move(carved.Mesh);
    }

    success.Mesh = std::move(out);
    return CarveOutcome::Success(std::move(success));
}

CarveOutcome CarveWithinLoopBounds(const BrushMesh& mesh, std::uint32_t face,
                                   const BrushOps::BrushRectFaceFrame& quad, Vec2d rectMin,
                                   Vec2d rectMax, const BrushFaceFrame& frame,
                                   std::span<const Vec2d> outline, bool pierce, float planarTol)
{
    if (face >= mesh.Faces.size())
        return CarveOutcome::Failure(CarveStatus::TopologyFailure);

    // The loop kernel reports no status and hands its input back on refusal;
    // the cell is the only evidence the cuts landed. A box covering the whole
    // face cuts nothing, and then the cell is the face itself.
    BrushMesh cut = BrushOps::InsertFaceLoopBounds(mesh, face, rectMin, rectMax);
    const std::optional<std::uint32_t> cell = BrushOps::FindRectFaceInFrame(cut, quad, rectMin, rectMax);
    if (!cell.has_value())
        return CarveOutcome::Failure(CarveStatus::TopologyFailure);
    const BrushFaceFrameResult cellFrame = FaceFrame(cut, *cell, planarTol);
    if (!cellFrame.Frame.has_value())
        return CarveOutcome::Failure(cellFrame.Status);

    std::vector<Vec2d> cellOutline;
    cellOutline.reserve(outline.size());
    for (const Vec2d& point : outline)
        cellOutline.push_back(cellFrame.Frame->ToFrame(frame.ToWorld(point)));

    if (OutlineFillsCell(cellOutline, cellFrame.Frame->Outline))
    {
        CarveSuccess success;
        if (!pierce)
        {
            success.Mesh = std::move(cut);
            return CarveOutcome::Success(std::move(success));
        }
        // The first loop pass existed only to find the cell and decide; the
        // tunnel is cut once, from the original mesh, by the kernel that has
        // always cut it.
        BrushMesh through = BrushOps::InsertFaceLoopBoundsThrough(mesh, face, rectMin, rectMax);
        if (through.Faces.size() == mesh.Faces.size() && through.Vertices.size() == mesh.Vertices.size())
            return CarveOutcome::Failure(CarveStatus::TopologyFailure);
        success.Mesh = std::move(through);
        return CarveOutcome::Success(std::move(success));
    }

    // A loop through every outline vertex on the rim that is not a corner --
    // where a jamb ends, where an apex or a diamond's point touches -- so the
    // faces outside the carve stay quads instead of gaining a fifth vertex
    // where the shape meets them. The rim side the point is on is a line of
    // constant U or V in the quad frame; the loop is on the other axis, through
    // the point. The loop kernel clamps its parameter rather than refusing a
    // cut too near an edge, so the ring is checked to have actually passed
    // through the point.
    const std::vector<Vec2d>& rim = cellFrame.Frame->Outline;
    const std::size_t count = cellOutline.size();
    for (std::size_t i = 0; i < count; ++i)
    {
        const Vec2d point = cellOutline[i];
        if (ClassifyPointInPolygon2D(rim, point, kCarveSnapTolerance) != PointPolygonRelation::Boundary)
            continue;
        bool atCorner = false;
        for (const Vec2d& corner : rim)
            atCorner = atCorner || Distance(corner, point) <= kCarveSnapTolerance;
        if (atCorner)
            continue;

        const Vec3d world = cellFrame.Frame->ToWorld(point);
        const Vec2d uv = QuadUv(quad, world);
        const bool onConstantU = std::abs(uv.X - rectMin.X) <= kCarveSnapTolerance
            || std::abs(uv.X - rectMax.X) <= kCarveSnapTolerance;
        const bool onConstantV = std::abs(uv.Y - rectMin.Y) <= kCarveSnapTolerance
            || std::abs(uv.Y - rectMax.Y) <= kCarveSnapTolerance;
        if (onConstantU == onConstantV)
            continue;
        const bool cutAlongU = onConstantV;
        std::optional<BrushMesh> looped =
            BrushOps::InsertFrameLoop(cut, quad, cutAlongU, cutAlongU ? uv.X : uv.Y);
        if (!looped.has_value())
            return CarveOutcome::Failure(CarveStatus::TopologyFailure);
        cut = std::move(*looped);
        bool reached = false;
        for (const BrushVertex& vertex : cut.Vertices)
            reached = reached
                || (vertex.Position - world).SqrMagnitude() <= kCarveSnapTolerance * kCarveSnapTolerance;
        if (!reached)
            return CarveOutcome::Failure(CarveStatus::TopologyFailure);
    }

    // The cells inside the box: every face on the host plane whose centre lies
    // in it. Named by their corners, because laying the shape renumbers.
    std::vector<FaceCorners> cells;
    const Vec3d normal = quad.AxisU.Cross(quad.AxisV);
    for (std::uint32_t f = 0; f < cut.Faces.size(); ++f)
    {
        if (BrushComputeFaceNormal(cut, cut.Faces[f]).Dot(normal) < 0.99f)
            continue;
        const Vec2d centre = QuadUv(quad, BrushFaceCentroid(cut, cut.Faces[f]));
        if (centre.X <= rectMin.X || centre.X >= rectMax.X || centre.Y <= rectMin.Y || centre.Y >= rectMax.Y)
            continue;
        cells.push_back(CornersOf(cut, f));
    }
    return CarveAcrossSurface(cut, cells, frame, outline, pierce, planarTol);
}

