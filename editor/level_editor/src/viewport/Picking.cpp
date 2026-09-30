#include "Picking.h"

#include "EditorViewport.h"
#include "ViewportMath.h"
#include "ViewportProjection.h"

#include "document/EditorScene.h"
#include "brush/BrushEvaluation.h"
#include "document/BrushPlacementFacts.h"
#include "document/SceneBrushWalk.h"
#include "meshedit/MeshElementKindTraits.h" // MeshElementKindCount
#include "meshedit/MeshElements.h"

#include "brush/CarveSurround.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <utility>
#include <vector>

bool IntersectRayFacePolygon(const Ray3d& ray, std::span<const Vec3d> corners, float& outDistance)
{
    if (corners.size() < 3)
        return false;

    // Newell: correct for n-gons and independent of which corner comes first.
    Vec3d normal{ 0.0f, 0.0f, 0.0f };
    for (std::size_t i = 0; i < corners.size(); ++i)
    {
        const Vec3d& a = corners[i];
        const Vec3d& b = corners[(i + 1) % corners.size()];
        normal.X += (a.Y - b.Y) * (a.Z + b.Z);
        normal.Y += (a.Z - b.Z) * (a.X + b.X);
        normal.Z += (a.X - b.X) * (a.Y + b.Y);
    }
    const float normalLength = normal.Magnitude();
    if (normalLength < 1e-12f)
        return false;
    normal = normal * (1.0f / normalLength);

    const double denominator = static_cast<double>(ray.Direction.Dot(normal));
    if (std::abs(denominator) < ViewportMath::kParallelEpsilon)
        return false;
    const double t = static_cast<double>((corners[0] - ray.Origin).Dot(normal)) / denominator;
    if (t < 0.0)
        return false;

    const std::array<Vec3d, 3> axes = { Vec3d{ 1, 0, 0 }, Vec3d{ 0, 1, 0 }, Vec3d{ 0, 0, 1 } };
    std::size_t seed = 0;
    for (std::size_t i = 1; i < axes.size(); ++i)
        if (std::abs(normal.Dot(axes[i])) < std::abs(normal.Dot(axes[seed])))
            seed = i;
    Vec3d axisU = axes[seed] - normal * axes[seed].Dot(normal);
    const float axisLength = axisU.Magnitude();
    if (axisLength < 1e-6f)
        return false;
    axisU = axisU * (1.0f / axisLength);
    const Vec3d axisV = normal.Cross(axisU);

    const Vec3d origin = corners[0];
    std::vector<Vec2d> polygon;
    polygon.reserve(corners.size());
    float extent = 0.0f;
    for (const Vec3d& corner : corners)
    {
        const Vec3d offset = corner - origin;
        polygon.push_back(Vec2d{ offset.Dot(axisU), offset.Dot(axisV) });
        extent = std::max(extent, offset.Magnitude());
    }
    const Vec3d offset = ray.Origin + ray.Direction * static_cast<float>(t) - origin;
    const Vec2d point{ offset.Dot(axisU), offset.Dot(axisV) };

    if (ClassifyPointInPolygon2D(polygon, point, std::max(1e-5f * extent, 1e-6f))
        == PointPolygonRelation::Outside)
        return false;
    outDistance = static_cast<float>(t);
    return true;
}

BrushPickMode PickModeForElementKind(MeshElementKind kind)
{
    static constexpr std::array<BrushPickMode, MeshElementKindCount> kModes = {
        BrushPickMode::EntityOnly, // Object
        BrushPickMode::VertexOnly, // Vertex
        BrushPickMode::EdgeOnly,   // Edge
        BrushPickMode::FaceOnly,   // Face
    };
    return kModes[static_cast<std::size_t>(kind)];
}

bool IntersectRayAabb(const Ray3d& ray, const Aabb3d& box, float& outNear)
{
    if (!box.IsValid())
        return false;
    double tMin = 0.0;
    double tMax = std::numeric_limits<double>::infinity();
    for (int axis = 0; axis < 3; ++axis)
    {
        const double origin = ray.Origin[axis];
        const double direction = ray.Direction[axis];
        const double lo = box.Min[axis];
        const double hi = box.Max[axis];
        if (std::abs(direction) < 1e-12)
        {
            if (origin < lo || origin > hi)
                return false; // parallel to this slab and outside it
            continue;
        }
        double t0 = (lo - origin) / direction;
        double t1 = (hi - origin) / direction;
        if (t0 > t1)
            std::swap(t0, t1);
        tMin = t0 > tMin ? t0 : tMin;
        tMax = t1 < tMax ? t1 : tMax;
        if (tMin > tMax)
            return false;
    }
    outNear = static_cast<float>(tMin);
    return true;
}

namespace
{
constexpr double kMaxPickDistance = 1.0e6;

// Nearest ray/brush-body hit. Tests the real transformed faces (the brush is a
// convex solid) instead of an origin-anchored box, so whole-brush selection
// matches the rendered geometry under rotation/scale and for off-origin meshes.
// One evaluated piece the ray's box test could not rule out. Faces are built
// on first use and shared by every test a pick runs on the piece, so a body
// hit and a face hit never tessellate the same piece twice.
struct RayPieceCandidate
{
    EntityId              Entity;
    const BrushEvaluated* Evaluated = nullptr;
    const BrushPiece*     Piece = nullptr;
    Transform3f           World;       // entity transform composed with the placement
    float                 Near = 0.0f; // ray entry into the piece's tight world box
    bool                  Live = false; // a brush with faces to edit, not a baked one's dormant source
    std::vector<FaceElement> Faces;
    bool                  FacesBuilt = false;

    const std::vector<FaceElement>& FacesOrBuild()
    {
        if (!FacesBuilt)
        {
            Faces = MeshElements::Faces(*Piece->Mesh, World);
            FacesBuilt = true;
        }
        return Faces;
    }
};

// The one traversal a ray pick makes: every visible (and, with skipLocked,
// unlocked) brush's pieces, kept only when the ray enters the piece's tight
// world box, ordered by entry distance so a caller that wants the nearest hit
// can stop early. Baked brushes keep their dormant source as a body target
// (Live == false) but offer no faces.
std::vector<RayPieceCandidate> GatherRayPieceCandidates(const EditorScene& scene, const Ray3d& ray,
                                                        bool skipLocked, EntityId restrictTo)
{
    std::vector<RayPieceCandidate> candidates;
    const BrushPlacementFacts& facts = scene.PlacementFacts();
    for (EntityId entity : scene.GetAllEntities())
    {
        if (restrictTo.IsValid() && entity != restrictTo)
            continue;
        if (!scene.IsEntityEffectivelyVisible(entity))
            continue;
        if (skipLocked && scene.IsEntityEffectivelyLocked(entity))
            continue;
        // A baked brush keeps its dormant source as a body target: it has no
        // placement facts, so it is bounded and placed by its source alone.
        if (scene.TryGetBrush(entity) == nullptr)
        {
            const BrushEvaluated* evaluated = scene.TryGetBrushPieces(entity);
            const Transform3f* transform = scene.TryGetWorldTransform(entity);
            const std::optional<Aabb3d> bounds = scene.SourceWorldBounds(entity);
            float near = 0.0f;
            if (evaluated == nullptr || transform == nullptr || !bounds.has_value()
                || evaluated->Pieces.empty() || !IntersectRayAabb(ray, *bounds, near))
                continue;
            RayPieceCandidate candidate;
            candidate.Entity = entity;
            candidate.Evaluated = evaluated;
            candidate.Piece = &evaluated->Pieces[evaluated->SourcePiece];
            candidate.World = *transform;
            candidate.Near = near;
            candidate.Live = false;
            candidates.push_back(std::move(candidate));
            continue;
        }
        float entityNear = 0.0f;
        const std::optional<Aabb3d> union_ = facts.GetEntityBounds(entity);
        if (!union_.has_value() || !IntersectRayAabb(ray, *union_, entityNear))
            continue;
        const BrushEvaluated* evaluated = facts.GetEvaluation(entity);
        const std::span<const Aabb3d> bounds = facts.GetPieceBounds(entity);
        const std::span<const Transform3f> placements = facts.GetPiecePlacements(entity);
        if (evaluated == nullptr)
            continue;
        for (const BrushPiece& piece : evaluated->Pieces)
        {
            if (piece.Ordinal >= bounds.size() || piece.Ordinal >= placements.size())
                continue;
            float near = 0.0f;
            if (!IntersectRayAabb(ray, bounds[piece.Ordinal], near))
                continue;
            RayPieceCandidate candidate;
            candidate.Entity = entity;
            candidate.Evaluated = evaluated;
            candidate.Piece = &piece;
            candidate.World = placements[piece.Ordinal];
            candidate.Near = near;
            candidate.Live = true;
            candidates.push_back(std::move(candidate));
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const RayPieceCandidate& a, const RayPieceCandidate& b) { return a.Near < b.Near; });
    return candidates;
}

// Pixel thresholds for screen-space element picking.
constexpr float kEdgePickPixels = 8.0f;
constexpr float kVertexPickPixels = 10.0f;

// True if solid brush geometry lies in front of `worldPoint` along the ray through
// its `pixel`, i.e. the point is hidden behind geometry and must not be pickable.
// The relative slack keeps the point's own incident faces (met at ~tPoint) from
// counting as occluders.
bool IsHidden(const EditorScene& scene, const ViewportProjection& projection, Vec3d worldPoint, ImVec2 pixel)
{
    const Ray3d ray = projection.RayThroughPixel(pixel);
    const double tPoint = (worldPoint - ray.Origin).Dot(ray.Direction);
    if (tPoint <= 0.0)
        return true; // behind the camera
    const double threshold = tPoint - std::max(0.05, tPoint * 0.01);

    // Candidates arrive nearest-box first: once a box starts beyond the
    // threshold no face in it or after it can occlude.
    for (RayPieceCandidate& candidate : GatherRayPieceCandidates(scene, ray, /*skipLocked*/ true, EntityId{}))
    {
        if (static_cast<double>(candidate.Near) >= threshold)
            break;
        if (!candidate.Live)
            continue;
        for (const FaceElement& face : candidate.FacesOrBuild())
        {
            float t = 0.0f;
            if (IntersectRayFacePolygon(ray, face.Corners, t) && static_cast<double>(t) < threshold)
                return true;
        }
    }
    return false;
}

// The point on segment [a, b] closest to the ray. Used to test occlusion at the true
// world point under the cursor: a screen-space interpolation lands at the wrong depth
// on an edge that recedes from the camera, so the edge's own face reads as a
// self-occluder. Same closest-param math as GizmoMath (ray.Direction assumed unit).
Vec3d ClosestPointOnSegmentToRay(Vec3d a, Vec3d b, const Ray3d& ray)
{
    const Vec3d u = b - a;
    const double uu = u.Dot(u);
    if (uu < 1.0e-18)
        return a;
    const Vec3d w0 = a - ray.Origin;
    const double bd = u.Dot(ray.Direction);
    const double denom = uu - bd * bd; // uu * sin^2(edge, ray)
    if (denom < uu * 1.0e-9)
        return a; // edge ~parallel to the ray: any point is ~equidistant
    const double s = std::clamp((bd * ray.Direction.Dot(w0) - u.Dot(w0)) / denom, 0.0, 1.0);
    return a + u * s;
}
}

SelectableRef PickingService::Pick(const EditorViewport& viewport,
                                   ImVec2 point,
                                   const EditorScene& scene,
                                   BrushPickRequest request) const
{
    if (request.Mode == BrushPickMode::EdgeOnly)
        return PickEdge(viewport, point, scene, request.RestrictTo);
    if (request.Mode == BrushPickMode::VertexOnly)
        return PickVertex(viewport, point, scene, request.RestrictTo);

    const Ray3d ray = BuildRay(viewport, point);
    return PickBrushElement(ray, scene, request);
}

SelectableRef PickingService::PickBrushElement(const Ray3d& ray,
                                               const EditorScene& scene,
                                               BrushPickRequest request) const
{
    PickCandidate bestCandidate{};
    bool hasBestCandidate = false;

    if (AllowsEntities(request) && ProxyProvider)
    {
        if (const auto proxy = ProxyProvider(ray, scene);
            proxy && (!request.RestrictTo.IsValid()
                      || proxy->first.Entity == request.RestrictTo))
        {
            bestCandidate = {
                .Ref = proxy->first,
                .Distance = proxy->second,
                .Priority = PriorityFor(request, proxy->first.Kind),
            };
            hasBestCandidate = true;
        }
    }

    const auto consider = [&](PickCandidate candidate)
    {
        candidate.Priority = PriorityFor(request, candidate.Ref.Kind);
        if (!IsBetterCandidate(candidate, bestCandidate, hasBestCandidate))
            return;
        bestCandidate = candidate;
        hasBestCandidate = true;
    };

    // Every evaluated piece is the entity's body (a baked brush's dormant
    // source keeps it clickable): the nearest piece hit selects the entity. A
    // face on any evaluated piece selects the SOURCE face it stands for, so
    // clicking a mirrored wall and applying a material edits the source and
    // every copy follows. Faces a modifier invented have no source and are not
    // selectable. Only live brushes have faces to edit.
    const RegistryId registry = scene.GetRegistry().Id;
    for (RayPieceCandidate& candidate :
         GatherRayPieceCandidates(scene, ray, /*skipLocked*/ true, request.RestrictTo))
    {
        const bool wantFaces = AllowsFaces(request) && candidate.Live;
        const bool wantBody = AllowsEntities(request);
        if (!wantFaces && !wantBody)
            continue;
        float bodyDistance = static_cast<float>(kMaxPickDistance);
        bool bodyHit = false;
        for (const FaceElement& face : candidate.FacesOrBuild())
        {
            float hitDistance = 0.0f;
            if (!IntersectRayFacePolygon(ray, face.Corners, hitDistance))
                continue;
            if (wantBody && hitDistance < bodyDistance)
            {
                bodyDistance = hitDistance;
                bodyHit = true;
            }
            // Back-facing surfaces aren't selectable: you pick what faces you.
            if (!wantFaces || face.Normal.Dot(ray.Direction) >= 0.0)
                continue;
            const std::optional<std::uint32_t> source =
                SourceFaceFor(*candidate.Evaluated, *candidate.Piece, face.Index);
            if (!source.has_value())
                continue;
            consider(PickCandidate{
                .Ref = SelectableRef::FaceSelection(registry, candidate.Entity, *source),
                .Distance = hitDistance,
                .Priority = 0u,
            });
        }
        if (bodyHit)
            consider(PickCandidate{
                .Ref = SelectableRef::EntitySelection(registry, candidate.Entity),
                .Distance = bodyDistance,
                .Priority = 0u,
            });
    }

    return hasBestCandidate ? bestCandidate.Ref : SelectableRef{};
}

bool PickingService::AllowsEntities(BrushPickRequest request)
{
    return request.Mode == BrushPickMode::EntityOnly
        || request.Mode == BrushPickMode::FacePreferred;
}

bool PickingService::AllowsFaces(BrushPickRequest request)
{
    return request.Mode == BrushPickMode::FacePreferred
        || request.Mode == BrushPickMode::FaceOnly;
}

uint8_t PickingService::PriorityFor(BrushPickRequest request, SelectableKind kind)
{
    switch (request.Mode)
    {
    case BrushPickMode::FaceOnly:
        return kind == SelectableKind::Face ? 0u : 255u;
    case BrushPickMode::FacePreferred:
        return kind == SelectableKind::Face ? 0u : 1u;
    case BrushPickMode::EntityOnly:
    default:
        return 0u;
    }
}

bool PickingService::IsBetterCandidate(const PickCandidate& candidate,
                                       const PickCandidate& best,
                                       bool hasBest)
{
    if (!hasBest)
        return true;
    if (candidate.Priority != best.Priority)
        return candidate.Priority < best.Priority;
    return candidate.Distance < best.Distance;
}

SelectableRef PickingService::PickEdge(const EditorViewport& viewport,
                                       ImVec2 point,
                                       const EditorScene& scene,
                                       EntityId restrictTo) const
{
    const ViewportProjection projection(viewport);
    // Occlusion only matters where geometry hides things: a wireframe (ortho) view
    // shows every edge, so all are selectable there.
    const bool occlude = viewport.Shading == ViewportShading::Solid;

    SelectableRef best{};
    float bestPixels = kEdgePickPixels;
    float bestDepth = 0.0f;

    // Edges and vertices are grabbed on the source only: a drag redirected from
    // a mirrored copy would move the source the other way. The elements are
    // the scene's retained facts, built once per topology and transform.
    for (EntityId entity : scene.GetAllEntities())
    {
        if (restrictTo.IsValid() && entity != restrictTo)
            continue;
        if (!scene.IsEntityEffectivelyVisible(entity) || scene.IsEntityEffectivelyLocked(entity))
            continue;
        const SourceWorldElements* elements = scene.PlacementFacts().GetSourceWorldElements(entity);
        if (elements == nullptr)
            continue;
        for (const EdgeElement& edge : elements->Edges)
        {
            const std::optional<ProjectedPoint> a = projection.WorldToPixel(edge.A);
            const std::optional<ProjectedPoint> b = projection.WorldToPixel(edge.B);
            if (!a.has_value() || !b.has_value())
                continue;

            const float pixels = ViewportProjection::DistancePointToSegment(point, a->Pixel, b->Pixel);
            const float depth = std::min(a->Depth, b->Depth);
            if (pixels > bestPixels)
                continue;
            if (best.IsValid() && pixels >= bestPixels && depth >= bestDepth)
                continue;
            // Skip the edge if the point the cursor grabs is hidden. Grab it in 3D
            // (closest point on the edge to the cursor ray) and test visibility along
            // the ray through that point, so the edge's own foreshortened face is met
            // at the edge (not nearer) and doesn't self-occlude. A partly-occluded
            // edge stays grabbable by its visible part; a back edge is rejected.
            // (PickVertex tests at the vertex's own pixel for the same reason.)
            if (occlude)
            {
                const Vec3d grab = ClosestPointOnSegmentToRay(edge.A, edge.B, projection.RayThroughPixel(point));
                if (const std::optional<ProjectedPoint> gp = projection.WorldToPixel(grab);
                    gp.has_value() && IsHidden(scene, projection, grab, gp->Pixel))
                    continue;
            }

            best = SelectableRef::EdgeSelection(scene.GetRegistry().Id, entity, edge.Index);
            bestPixels = pixels;
            bestDepth = depth;
        }
    }

    return best;
}

SelectableRef PickingService::PickVertex(const EditorViewport& viewport,
                                         ImVec2 point,
                                         const EditorScene& scene,
                                         EntityId restrictTo) const
{
    const ViewportProjection projection(viewport);
    const bool occlude = viewport.Shading == ViewportShading::Solid;

    SelectableRef best{};
    float bestPixels = kVertexPickPixels;
    float bestDepth = 0.0f;

    for (EntityId entity : scene.GetAllEntities())
    {
        if (restrictTo.IsValid() && entity != restrictTo)
            continue;
        if (!scene.IsEntityEffectivelyVisible(entity) || scene.IsEntityEffectivelyLocked(entity))
            continue;
        const SourceWorldElements* elements = scene.PlacementFacts().GetSourceWorldElements(entity);
        if (elements == nullptr)
            continue;
        for (const VertexElement& vertex : elements->Vertices)
        {
            const std::optional<ProjectedPoint> projected = projection.WorldToPixel(vertex.Position);
            if (!projected.has_value())
                continue;

            const float dx = point.x - projected->Pixel.x;
            const float dy = point.y - projected->Pixel.y;
            const float pixels = std::sqrt(dx * dx + dy * dy);
            if (pixels > bestPixels)
                continue;
            if (best.IsValid() && pixels >= bestPixels && projected->Depth >= bestDepth)
                continue;
            // Skip vertices hidden behind geometry (solid views only).
            if (occlude && IsHidden(scene, projection, vertex.Position, projected->Pixel))
                continue;

            best = SelectableRef::VertexSelection(scene.GetRegistry().Id, entity, vertex.Index);
            bestPixels = pixels;
            bestDepth = projected->Depth;
        }
    }

    return best;
}

SelectableRef PickingService::PickLoopSeedEdge(const EditorViewport& viewport,
                                               ImVec2 point,
                                               const EditorScene& scene,
                                               MeshElementKind mode,
                                               EntityId restrictTo) const
{
    if (mode == MeshElementKind::Edge)
        return PickEdge(viewport, point, scene, restrictTo);
    if (mode != MeshElementKind::Face)
        return {};

    // Face mode seeds from the edge of the picked face nearest the cursor: ray-pick
    // the face, then pick its screen-nearest loop edge.
    const Ray3d ray = BuildRay(viewport, point);
    const SelectableRef face = PickBrushElement(ray, scene,
        BrushPickRequest{ .Mode = BrushPickMode::FaceOnly, .RestrictTo = restrictTo });
    if (!face.IsFace())
        return {};

    const BrushMesh* mesh = scene.TryGetBrushMesh(face.Entity);
    const BrushPlacementFacts& facts = scene.PlacementFacts();
    const SourceWorldElements* elements = facts.GetSourceWorldElements(face.Entity);
    if (mesh == nullptr || elements == nullptr || face.ElementId >= mesh->Faces.size())
        return {};

    // The face's loop edges resolve to global edge ids through the retained
    // edge table.
    const ViewportProjection projection(viewport);
    const std::vector<std::uint32_t>& loop = mesh->Faces[face.ElementId].Loop;

    SelectableRef best{};
    float bestPixels = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < loop.size(); ++i)
    {
        const std::uint32_t va = loop[i];
        const std::uint32_t vb = loop[(i + 1) % loop.size()];
        const std::optional<std::uint32_t> index = facts.SourceEdgeIndexOf(face.Entity, va, vb);
        if (!index.has_value() || *index >= elements->Edges.size())
            continue;
        const EdgeElement& edge = elements->Edges[*index];

        const std::optional<ProjectedPoint> a = projection.WorldToPixel(edge.A);
        const std::optional<ProjectedPoint> b = projection.WorldToPixel(edge.B);
        if (!a.has_value() || !b.has_value())
            continue;

        const float pixels = ViewportProjection::DistancePointToSegment(point, a->Pixel, b->Pixel);
        if (pixels >= bestPixels)
            continue;

        bestPixels = pixels;
        best = SelectableRef::EdgeSelection(scene.GetRegistry().Id, face.Entity, edge.Index);
    }

    return best;
}

std::vector<SelectableRef> PickingService::PickInRect(const EditorViewport& viewport,
                                                      ImVec2 rectMin,
                                                      ImVec2 rectMax,
                                                      const EditorScene& scene,
                                                      MeshElementKind mode) const
{
    const float minX = std::min(rectMin.x, rectMax.x);
    const float minY = std::min(rectMin.y, rectMax.y);
    const float maxX = std::max(rectMin.x, rectMax.x);
    const float maxY = std::max(rectMin.y, rectMax.y);
    const ViewportProjection projection(viewport);
    const RegistryId registry = scene.GetRegistry().Id;

    const auto inside = [&](ImVec2 p) {
        return p.x >= minX && p.x <= maxX && p.y >= minY && p.y <= maxY;
    };

    std::vector<SelectableRef> result;
    const auto pushUnique = [&](const SelectableRef& ref) {
        if (std::find(result.begin(), result.end(), ref) == result.end())
            result.push_back(ref);
    };

    // Object and face modes see every evaluated piece (several pieces resolve
    // to one entity or one source face, hence the dedupe); edge and vertex
    // modes see the source only, as PickEdge/PickVertex do.
    const BrushPlacementFacts& facts = scene.PlacementFacts();
    if (mode == MeshElementKind::Vertex || mode == MeshElementKind::Edge)
    {
        for (EntityId entity : scene.GetAllEntities())
        {
            if (!scene.IsEntityEffectivelyVisible(entity) || scene.IsEntityEffectivelyLocked(entity))
                continue;
            const SourceWorldElements* elements = facts.GetSourceWorldElements(entity);
            if (elements == nullptr)
                continue;
            if (mode == MeshElementKind::Vertex)
            {
                for (const VertexElement& vertex : elements->Vertices)
                    if (const auto p = projection.WorldToPixel(vertex.Position); p && inside(p->Pixel))
                        result.push_back(SelectableRef::VertexSelection(registry, entity, vertex.Index));
            }
            else
            {
                for (const EdgeElement& edge : elements->Edges)
                    if (const auto p = projection.WorldToPixel(edge.Mid); p && inside(p->Pixel))
                        result.push_back(SelectableRef::EdgeSelection(registry, entity, edge.Index));
            }
        }
        return result;
    }

    // Object and face modes test every piece's projection, which is per drag
    // frame and per camera, so it is computed here from the retained
    // placements rather than retained itself.
    const auto forEachPiece = [&](auto&& fn)
    {
        for (EntityId entity : scene.GetAllEntities())
        {
            if (!scene.IsEntityEffectivelyVisible(entity) || scene.IsEntityEffectivelyLocked(entity)
                || scene.TryGetBrush(entity) == nullptr)
                continue;
            const BrushEvaluated* evaluated = facts.GetEvaluation(entity);
            const std::span<const Transform3f> placements = facts.GetPiecePlacements(entity);
            if (evaluated == nullptr)
                continue;
            for (const BrushPiece& piece : evaluated->Pieces)
                if (piece.Ordinal < placements.size())
                    fn(entity, piece, placements[piece.Ordinal]);
        }
    };
    forEachPiece([&](EntityId entity, const BrushPiece& piece, const Transform3f& transform)
    {
        const BrushMesh& mesh = *piece.Mesh;
        if (mode == MeshElementKind::Object)
        {
            // Overlap of the real geometry's projected screen rectangle with the
            // marquee — projecting the actual mesh vertices, not an origin box.
            float bxMin = std::numeric_limits<float>::max();
            float byMin = std::numeric_limits<float>::max();
            float bxMax = std::numeric_limits<float>::lowest();
            float byMax = std::numeric_limits<float>::lowest();
            bool any = false;
            for (const VertexElement& vertex : MeshElements::Vertices(mesh, transform))
            {
                if (const std::optional<ProjectedPoint> p = projection.WorldToPixel(vertex.Position))
                {
                    bxMin = std::min(bxMin, p->Pixel.x);
                    byMin = std::min(byMin, p->Pixel.y);
                    bxMax = std::max(bxMax, p->Pixel.x);
                    byMax = std::max(byMax, p->Pixel.y);
                    any = true;
                }
            }
            if (any && bxMin <= maxX && bxMax >= minX && byMin <= maxY && byMax >= minY)
                pushUnique(SelectableRef::EntitySelection(registry, entity));
            return;
        }

        if (mode != MeshElementKind::Face)
            return;
        const BrushEvaluated* evaluated = facts.GetEvaluation(entity);
        if (evaluated == nullptr)
            return;
        for (const FaceElement& face : MeshElements::Faces(mesh, transform))
        {
            const auto p = projection.WorldToPixel(face.Center);
            if (!p || !inside(p->Pixel))
                continue;
            if (const std::optional<std::uint32_t> source = SourceFaceFor(*evaluated, piece, face.Index))
                pushUnique(SelectableRef::FaceSelection(registry, entity, *source));
        }
    });

    return result;
}

std::optional<Vec3d> PickingService::ProjectPointToGrid(const EditorViewport& viewport,
                                                        ImVec2 point,
                                                        const GridSettings& settings) const
{
    return ProjectPointToPlane(viewport, point, viewport.GetGrid(settings));
}

std::optional<Vec3d> PickingService::ProjectPointToPlane(const EditorViewport& viewport,
                                                         ImVec2 point,
                                                         const GridPlane& plane) const
{
    const Ray3d ray = BuildRay(viewport, point);
    const Vec3d normal = plane.AxisU.Cross(plane.AxisV).Normalized();
    const double denominator = normal.Dot(ray.Direction);
    if (std::abs(denominator) < ViewportMath::kParallelEpsilon)
        return std::nullopt;

    const double distance = normal.Dot(plane.Origin - ray.Origin) / denominator;
    if (distance < 0.0)
        return std::nullopt;

    return plane.Snap(ray.PointAt(static_cast<float>(distance)));
}

std::optional<SurfaceHit> PickingService::PickSurface(const EditorViewport& viewport,
                                                      ImVec2 point,
                                                      const EditorScene& scene) const
{
    const Ray3d ray = BuildRay(viewport, point);
    float best = static_cast<float>(kMaxPickDistance);
    std::optional<SurfaceHit> hit;
    for (RayPieceCandidate& candidate : GatherRayPieceCandidates(scene, ray, /*skipLocked*/ true, EntityId{}))
    {
        if (candidate.Near >= best)
            break; // nearest-box order: nothing after this can beat the hit in hand
        if (!candidate.Live)
            continue;
        for (const FaceElement& face : candidate.FacesOrBuild())
        {
            float distance = 0.0f;
            if (IntersectRayFacePolygon(ray, face.Corners, distance) && distance < best)
            {
                best = distance;
                hit = SurfaceHit{ .Point = ray.PointAt(distance), .Normal = face.Normal };
            }
        }
    }
    return hit;
}

Ray3d PickingService::BuildRay(const EditorViewport& viewport, ImVec2 point) const
{
    return ViewportProjection(viewport).RayThroughPixel(point);
}
