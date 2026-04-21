#include "Picking.h"

#include "EditorViewport.h"

#include "../level/LevelScene.h"

#include <math/geometry/3d/Aabb3d.h>

#include <algorithm>
#include <cmath>

namespace
{
bool IntersectRayAabb(const Ray3d& ray, const Aabb3d& bounds, float& outDistance)
{
    double minDistance = 0.0;
    double maxDistance = 1000000.0;

    for (int axis = 0; axis < 3; ++axis)
    {
        const double origin = ray.Origin[axis];
        const double direction = ray.Direction[axis];
        const double minBound = bounds.Min[axis];
        const double maxBound = bounds.Max[axis];

        if (std::abs(direction) < 1e-8)
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
                                   const LevelScene& scene) const
{
    const Ray3d ray = BuildRay(viewport, point);

    float bestDistance = 1000000.0f;
    EntityId bestEntity = {};
    for (EntityId entity : scene.GetAllEntities())
    {
        const Transform3f* transform = scene.TryGetTransform(entity);
        const CubePrimitive* cube = scene.TryGetCube(entity);
        if (transform == nullptr || cube == nullptr)
            continue;

        const Aabb3d bounds = Aabb3d::FromCenterHalfExtent(transform->Position, cube->HalfExtents);
        float hitDistance = 0.0f;
        if (!IntersectRayAabb(ray, bounds, hitDistance))
            continue;
        if (hitDistance >= bestDistance)
            continue;

        bestDistance = hitDistance;
        bestEntity = entity;
    }

    if (!bestEntity.IsValid())
        return {};

    return SelectableRef{
        .Registry = scene.GetRegistry().Id,
        .Entity = bestEntity,
    };
}

std::optional<Vec3d> PickingService::ProjectPointToGrid(const EditorViewport& viewport, ImVec2 point) const
{
    const Ray3d ray = BuildRay(viewport, point);
    const Vec3d normal = viewport.ActiveGrid.AxisU.Cross(viewport.ActiveGrid.AxisV).Normalized();
    const double denominator = normal.Dot(ray.Direction);
    if (std::abs(denominator) < 1e-8)
        return std::nullopt;

    const double distance = normal.Dot(viewport.ActiveGrid.Origin - ray.Origin) / denominator;
    if (distance < 0.0)
        return std::nullopt;

    return viewport.ActiveGrid.Snap(ray.PointAt(static_cast<float>(distance)));
}

Ray3d PickingService::BuildRay(const EditorViewport& viewport, ImVec2 point) const
{
    const float width = viewport.RegionMax.x - viewport.RegionMin.x;
    const float height = viewport.RegionMax.y - viewport.RegionMin.y;
    if (width <= 0.0f || height <= 0.0f)
        return {};

    const float localX = (point.x - viewport.RegionMin.x) / width;
    const float localY = (point.y - viewport.RegionMin.y) / height;
    const float clipX = localX * 2.0f - 1.0f;
    const float clipY = localY * 2.0f - 1.0f;

    const CameraRenderData renderData = viewport.BuildRenderData();
    const Mat4 inverseViewProjection = renderData.ViewProjection.Inverse();

    const Vec4 nearClip(clipX, clipY, 0.0f, 1.0f);
    const Vec4 farClip(clipX, clipY, 1.0f, 1.0f);

    Vec4 nearWorld = inverseViewProjection * nearClip;
    Vec4 farWorld = inverseViewProjection * farClip;
    nearWorld /= nearWorld.W;
    farWorld /= farWorld.W;

    const Vec3d origin(nearWorld.X, nearWorld.Y, nearWorld.Z);
    const Vec3d target(farWorld.X, farWorld.Y, farWorld.Z);
    return Ray3d(origin, (target - origin).Normalized());
}
