#include "Picking.h"

#include "EditorViewport.h"

#include "../level/BrushGeometry.h"
#include "../level/LevelScene.h"

#include <math/geometry/3d/Aabb3d.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
constexpr double kMaxPickDistance = 1.0e6;
constexpr double kParallelEpsilon = 1.0e-8;
constexpr float kVulkanNdcNear = 0.0f;
constexpr float kVulkanNdcFar = 1.0f;
constexpr float kNdcRangeScale = 2.0f;
constexpr float kNdcRangeOffset = 1.0f;

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
}

SelectableRef PickingService::Pick(const EditorViewport& viewport,
                                   ImVec2 point,
                                   const LevelScene& scene,
                                   BrushPickRequest request) const
{
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
        return kind == SelectableKind::BrushFace ? 0u : 255u;
    case BrushPickMode::FacePreferred:
        return kind == SelectableKind::BrushFace ? 0u : 1u;
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
    for (const BrushFaceDescriptor& face : BrushGeometry::EnumerateFaces(scene, entity))
    {
        if (!face.Ref.IsValid())
            continue;

        float hitDistance = 0.0f;
        if (!IntersectRayAabb(ray, face.Geometry.Bounds, hitDistance))
            continue;

        outCandidates.push_back(PickCandidate{
            .Ref = face.Ref,
            .Distance = hitDistance,
            .Priority = 0u,
        });
    }
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
    const float width = viewport.RegionMax.x - viewport.RegionMin.x;
    const float height = viewport.RegionMax.y - viewport.RegionMin.y;
    if (width <= 0.0f || height <= 0.0f)
        return {};

    const float localX = (point.x - viewport.RegionMin.x) / width;
    const float localY = (point.y - viewport.RegionMin.y) / height;
    const float clipX = localX * kNdcRangeScale - kNdcRangeOffset;
    const float clipY = localY * kNdcRangeScale - kNdcRangeOffset;

    const CameraRenderData renderData = viewport.BuildRenderData();
    const Mat4 inverseViewProjection = renderData.ViewProjection.Inverse();

    const Vec4 nearClip(clipX, clipY, kVulkanNdcNear, 1.0f);
    const Vec4 farClip(clipX, clipY, kVulkanNdcFar, 1.0f);

    Vec4 nearWorld = inverseViewProjection * nearClip;
    Vec4 farWorld = inverseViewProjection * farClip;
    nearWorld /= nearWorld.W;
    farWorld /= farWorld.W;

    const Vec3d origin(nearWorld.X, nearWorld.Y, nearWorld.Z);
    const Vec3d target(farWorld.X, farWorld.Y, farWorld.Z);
    return Ray3d(origin, (target - origin).Normalized());
}
