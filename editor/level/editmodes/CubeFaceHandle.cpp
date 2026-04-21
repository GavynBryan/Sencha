#include "CubeFaceHandle.h"

#include "../../level/interactions/CubeResizeDragInteraction.h"
#include "../../viewport/EditorCamera.h"
#include "../../viewport/EditorViewport.h"

#include <math/geometry/3d/Aabb3d.h>
#include <math/geometry/3d/Ray3d.h>
#include <math/Vec.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace
{
constexpr double kMaxDist = 1.0e6;
constexpr double kParallelEps = 1.0e-8;
constexpr float kHandleThickness = 0.12f;

bool RayIntersectsAabb(const Ray3d& ray, const Aabb3d& bounds, float& outDist)
{
    double tMin = 0.0;
    double tMax = kMaxDist;
    for (int i = 0; i < 3; ++i)
    {
        const double o = ray.Origin[i];
        const double d = ray.Direction[i];
        const double lo = bounds.Min[i];
        const double hi = bounds.Max[i];
        if (std::abs(d) < kParallelEps)
        {
            if (o < lo || o > hi) return false;
            continue;
        }
        const double invD = 1.0 / d;
        double t0 = (lo - o) * invD;
        double t1 = (hi - o) * invD;
        if (t0 > t1) std::swap(t0, t1);
        tMin = std::max(tMin, t0);
        tMax = std::min(tMax, t1);
        if (tMax < tMin) return false;
    }
    outDist = static_cast<float>(tMin);
    return true;
}

Ray3d BuildRay(const EditorViewport& viewport, ImVec2 point)
{
    const float w = viewport.RegionMax.x - viewport.RegionMin.x;
    const float h = viewport.RegionMax.y - viewport.RegionMin.y;
    if (w <= 0.0f || h <= 0.0f) return {};

    const float lx = (point.x - viewport.RegionMin.x) / w;
    const float ly = (point.y - viewport.RegionMin.y) / h;
    const float cx = lx * 2.0f - 1.0f;
    const float cy = ly * 2.0f - 1.0f;

    const CameraRenderData rd = viewport.BuildRenderData();
    const Mat4 invVP = rd.ViewProjection.Inverse();

    const Vec4 near4(cx, cy, 0.0f, 1.0f);
    const Vec4 far4(cx, cy, 1.0f, 1.0f);
    Vec4 nearW = invVP * near4; nearW /= nearW.W;
    Vec4 farW  = invVP * far4;  farW  /= farW.W;

    const Vec3d origin(nearW.X, nearW.Y, nearW.Z);
    const Vec3d target(farW.X,  farW.Y,  farW.Z);
    return Ray3d(origin, (target - origin).Normalized());
}
}

CubeFaceHandle::CubeFaceHandle(EntityId entity, int faceIndex, LevelScene& scene, LevelDocument& document)
    : Entity(entity)
    , FaceIndex(faceIndex)
    , Scene(scene)
    , Document(document)
{
}

HandleHit CubeFaceHandle::HitTest(const EditorViewport& viewport, ImVec2 screenPos) const
{
    if (viewport.Camera.ActiveMode != EditorCamera::Mode::Orthographic)
        return {};

    const Transform3f* t = Scene.TryGetTransform(Entity);
    const CubePrimitive* cube = Scene.TryGetCube(Entity);
    if (!t || !cube) return {};

    const int axis = FaceIndex / 2;
    const float sign = (FaceIndex % 2 == 0) ? 1.0f : -1.0f;
    const float facePos = t->Position[axis] + sign * cube->HalfExtents[axis];

    Aabb3d slab;
    slab.Min = Aabb3d::FromCenterHalfExtent(t->Position, cube->HalfExtents).Min;
    slab.Max = Aabb3d::FromCenterHalfExtent(t->Position, cube->HalfExtents).Max;
    slab.Min[axis] = facePos - kHandleThickness;
    slab.Max[axis] = facePos + kHandleThickness;

    const Ray3d ray = BuildRay(viewport, screenPos);
    float dist = 0.0f;
    if (!RayIntersectsAabb(ray, slab, dist))
        return {};

    return HandleHit{ .Hit = true, .Distance = dist };
}

std::unique_ptr<IInteraction> CubeFaceHandle::BeginDrag(ToolContext& /*ctx*/,
                                                         const EditorViewport& /*viewport*/,
                                                         ImVec2 /*screenPos*/) const
{
    const Transform3f* t = Scene.TryGetTransform(Entity);
    const CubePrimitive* cube = Scene.TryGetCube(Entity);
    if (!t || !cube) return nullptr;

    return std::make_unique<CubeResizeDragInteraction>(
        Entity, FaceIndex, *t, cube->HalfExtents, Scene, Document);
}
