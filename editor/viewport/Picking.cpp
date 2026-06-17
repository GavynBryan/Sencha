#include "Picking.h"

#include "EditorViewport.h"
#include "ViewportProjection.h"

#include "../level/BrushGeometry.h"
#include "../level/LevelScene.h"
#include "../meshedit/MeshElements.h"

#include <math/geometry/3d/Aabb3d.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
constexpr double kMaxPickDistance = 1.0e6;
constexpr double kParallelEpsilon = 1.0e-8;

bool IntersectRayAabb(const Ray3d& ray, const Aabb3d& bounds, float& outDistance)
{
    double minDistance = 0.0;
    double maxDistance = kMaxPickDistance;

    for (int axis = 0; axis < 3; ++axis)
    {
        const double origin = ray.Origin[axis];
        const double direction = ray.Direction[axis];
        const double minBound = bounds.Min[axis];
        const double maxBound = bounds.Max[axis];

        if (std::abs(direction) < kParallelEpsilon)
        {
            if (origin < minBound || origin > maxBound)
                return false;
            continue;
        }

        const double inverseDirection = 1.0 / direction;
        double t0 = (minBound - origin) * inverseDirection;
        double t1 = (maxBound - origin) * inverseDirection;
        if (t0 > t1)
            std::swap(t0, t1);

        minDistance = std::max(minDistance, t0);
        maxDistance = std::min(maxDistance, t1);
        if (maxDistance < minDistance)
            return false;
    }

    outDistance = static_cast<float>(minDistance);
    return true;
}

// Möller–Trumbore ray/triangle.
bool IntersectRayTriangle(const Ray3d& ray, const Vec3d& a, const Vec3d& b, const Vec3d& c, double& outT)
{
    const Vec3d e1 = b - a;
    const Vec3d e2 = c - a;
    const Vec3d p = ray.Direction.Cross(e2);
    const double det = e1.Dot(p);
    if (std::abs(det) < kParallelEpsilon)
        return false;
    const double inv = 1.0 / det;
    const Vec3d tvec = ray.Origin - a;
    const double u = tvec.Dot(p) * inv;
    if (u < 0.0 || u > 1.0)
        return false;
    const Vec3d q = tvec.Cross(e1);
    const double v = ray.Direction.Dot(q) * inv;
    if (v < 0.0 || u + v > 1.0)
        return false;
    const double t = e2.Dot(q) * inv;
    if (t < 0.0)
        return false;
    outT = t;
    return true;
}

// Ray vs a planar face polygon via triangle fan; nearest hit.
bool IntersectRayPolygon(const Ray3d& ray, const std::vector<Vec3d>& corners, float& outDistance)
{
    if (corners.size() < 3)
        return false;
    bool hit = false;
    double best = kMaxPickDistance;
    for (std::size_t i = 1; i + 1 < corners.size(); ++i)
    {
        double t = 0.0;
        if (IntersectRayTriangle(ray, corners[0], corners[i], corners[i + 1], t) && t < best)
        {
            best = t;
            hit = true;
        }
    }
    if (hit)
        outDistance = static_cast<float>(best);
    return hit;
}

// Pixel thresholds for screen-space element picking.
constexpr float kEdgePickPixels = 8.0f;
constexpr float kVertexPickPixels = 10.0f;
}

SelectableRef PickingService::Pick(const EditorViewport& viewport,
                                   ImVec2 point,
                                   const LevelScene& scene,
                                   BrushPickRequest request) const
{
    if (request.Mode == BrushPickMode::EdgeOnly)
        return PickEdge(viewport, point, scene);
    if (request.Mode == BrushPickMode::VertexOnly)
        return PickVertex(viewport, point, scene);

    const Ray3d ray = BuildRay(viewport, point);
    return PickBrushElement(ray, scene, request);
}

SelectableRef PickingService::PickBrushElement(const Ray3d& ray,
                                               const LevelScene& scene,
                                               BrushPickRequest request) const
{
    PickCandidate bestCandidate{};
    bool hasBestCandidate = false;
    std::vector<PickCandidate> candidates;
    candidates.reserve(6);

    for (EntityId entity : scene.GetAllEntities())
    {
        if (!scene.IsEntityVisible(entity) || scene.IsEntityLocked(entity))
            continue;

        candidates.clear();

        if (AllowsFaces(request))
            GatherBrushFaceCandidates(ray, scene, entity, candidates);

        if (AllowsEntities(request))
        {
            if (const auto body = MakeBrushBodyCandidate(ray, scene, entity))
                candidates.push_back(*body);
        }

        for (const PickCandidate& candidate : candidates)
        {
            if (!candidate.Ref.IsValid())
                continue;

            PickCandidate rankedCandidate = candidate;
            rankedCandidate.Priority = PriorityFor(request, rankedCandidate.Ref.Kind);

            if (!IsBetterCandidate(rankedCandidate, bestCandidate, hasBestCandidate))
                continue;

            bestCandidate = rankedCandidate;
            hasBestCandidate = true;
        }
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

std::optional<PickingService::PickCandidate> PickingService::MakeBrushBodyCandidate(const Ray3d& ray,
                                                                                    const LevelScene& scene,
                                                                                    EntityId entity) const
{
    const std::optional<BrushState> state = BrushGeometry::TryGetState(scene, entity);
    if (!state.has_value())
        return std::nullopt;

    const Aabb3d bounds = BrushGeometry::ComputeBounds(*state);
    float hitDistance = 0.0f;
    if (!IntersectRayAabb(ray, bounds, hitDistance))
        return std::nullopt;

    return PickCandidate{
        .Ref = SelectableRef::EntitySelection(scene.GetRegistry().Id, entity),
        .Distance = hitDistance,
        .Priority = 0u,
    };
}

void PickingService::GatherBrushFaceCandidates(const Ray3d& ray,
                                               const LevelScene& scene,
                                               EntityId entity,
                                               std::vector<PickCandidate>& outCandidates) const
{
    const Transform3f* transform = scene.TryGetTransform(entity);
    const BrushMesh* mesh = scene.TryGetBrushMesh(entity);
    if (transform == nullptr || mesh == nullptr)
        return;

    for (const FaceElement& face : MeshElements::Faces(*mesh, *transform))
    {
        float hitDistance = 0.0f;
        if (!IntersectRayPolygon(ray, face.Corners, hitDistance))
            continue;

        outCandidates.push_back(PickCandidate{
            .Ref = SelectableRef::FaceSelection(scene.GetRegistry().Id, entity, face.Index),
            .Distance = hitDistance,
            .Priority = 0u,
        });
    }
}

SelectableRef PickingService::PickEdge(const EditorViewport& viewport,
                                       ImVec2 point,
                                       const LevelScene& scene) const
{
    const ViewportProjection projection(viewport);

    SelectableRef best{};
    float bestPixels = kEdgePickPixels;
    float bestDepth = 0.0f;

    for (EntityId entity : scene.GetAllEntities())
    {
        if (!scene.IsEntityVisible(entity) || scene.IsEntityLocked(entity))
            continue;

        const Transform3f* transform = scene.TryGetTransform(entity);
        const BrushMesh* mesh = scene.TryGetBrushMesh(entity);
        if (transform == nullptr || mesh == nullptr)
            continue;

        for (const EdgeElement& edge : MeshElements::Edges(*mesh, *transform))
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

            best = SelectableRef::EdgeSelection(scene.GetRegistry().Id, entity, edge.Index);
            bestPixels = pixels;
            bestDepth = depth;
        }
    }

    return best;
}

SelectableRef PickingService::PickVertex(const EditorViewport& viewport,
                                         ImVec2 point,
                                         const LevelScene& scene) const
{
    const ViewportProjection projection(viewport);

    SelectableRef best{};
    float bestPixels = kVertexPickPixels;
    float bestDepth = 0.0f;

    for (EntityId entity : scene.GetAllEntities())
    {
        if (!scene.IsEntityVisible(entity) || scene.IsEntityLocked(entity))
            continue;

        const Transform3f* transform = scene.TryGetTransform(entity);
        const BrushMesh* mesh = scene.TryGetBrushMesh(entity);
        if (transform == nullptr || mesh == nullptr)
            continue;

        for (const VertexElement& vertex : MeshElements::Vertices(*mesh, *transform))
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

            best = SelectableRef::VertexSelection(scene.GetRegistry().Id, entity, vertex.Index);
            bestPixels = pixels;
            bestDepth = projected->Depth;
        }
    }

    return best;
}

std::vector<SelectableRef> PickingService::PickInRect(const EditorViewport& viewport,
                                                      ImVec2 rectMin,
                                                      ImVec2 rectMax,
                                                      const LevelScene& scene,
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

    for (EntityId entity : scene.GetAllEntities())
    {
        if (!scene.IsEntityVisible(entity) || scene.IsEntityLocked(entity))
            continue;

        if (mode == MeshElementKind::Object)
        {
            const std::optional<BrushState> state = BrushGeometry::TryGetState(scene, entity);
            if (!state.has_value())
                continue;

            // Overlap of the projected bounds' screen rectangle with the marquee.
            const Aabb3d bounds = BrushGeometry::ComputeBounds(*state);
            float bxMin = std::numeric_limits<float>::max();
            float byMin = std::numeric_limits<float>::max();
            float bxMax = std::numeric_limits<float>::lowest();
            float byMax = std::numeric_limits<float>::lowest();
            bool any = false;
            for (int corner = 0; corner < 8; ++corner)
            {
                const Vec3d point(
                    (corner & 1) ? bounds.Max.X : bounds.Min.X,
                    (corner & 2) ? bounds.Max.Y : bounds.Min.Y,
                    (corner & 4) ? bounds.Max.Z : bounds.Min.Z);
                if (const std::optional<ProjectedPoint> p = projection.WorldToPixel(point))
                {
                    bxMin = std::min(bxMin, p->Pixel.x);
                    byMin = std::min(byMin, p->Pixel.y);
                    bxMax = std::max(bxMax, p->Pixel.x);
                    byMax = std::max(byMax, p->Pixel.y);
                    any = true;
                }
            }
            if (any && bxMin <= maxX && bxMax >= minX && byMin <= maxY && byMax >= minY)
                result.push_back(SelectableRef::EntitySelection(registry, entity));
            continue;
        }

        const Transform3f* transform = scene.TryGetTransform(entity);
        const BrushMesh* mesh = scene.TryGetBrushMesh(entity);
        if (transform == nullptr || mesh == nullptr)
            continue;

        switch (mode)
        {
        case MeshElementKind::Vertex:
            for (const VertexElement& vertex : MeshElements::Vertices(*mesh, *transform))
                if (const auto p = projection.WorldToPixel(vertex.Position); p && inside(p->Pixel))
                    result.push_back(SelectableRef::VertexSelection(registry, entity, vertex.Index));
            break;
        case MeshElementKind::Edge:
            for (const EdgeElement& edge : MeshElements::Edges(*mesh, *transform))
                if (const auto p = projection.WorldToPixel(edge.Mid); p && inside(p->Pixel))
                    result.push_back(SelectableRef::EdgeSelection(registry, entity, edge.Index));
            break;
        case MeshElementKind::Face:
            for (const FaceElement& face : MeshElements::Faces(*mesh, *transform))
                if (const auto p = projection.WorldToPixel(face.Center); p && inside(p->Pixel))
                    result.push_back(SelectableRef::FaceSelection(registry, entity, face.Index));
            break;
        case MeshElementKind::Object:
        default:
            break;
        }
    }

    return result;
}

std::optional<Vec3d> PickingService::ProjectPointToGrid(const EditorViewport& viewport, ImVec2 point) const
{
    const Ray3d ray = BuildRay(viewport, point);
    const GridPlane grid = viewport.GetGrid();
    const Vec3d normal = grid.AxisU.Cross(grid.AxisV).Normalized();
    const double denominator = normal.Dot(ray.Direction);
    if (std::abs(denominator) < kParallelEpsilon)
        return std::nullopt;

    const double distance = normal.Dot(grid.Origin - ray.Origin) / denominator;
    if (distance < 0.0)
        return std::nullopt;

    return grid.Snap(ray.PointAt(static_cast<float>(distance)));
}

Ray3d PickingService::BuildRay(const EditorViewport& viewport, ImVec2 point) const
{
    return ViewportProjection(viewport).RayThroughPixel(point);
}
