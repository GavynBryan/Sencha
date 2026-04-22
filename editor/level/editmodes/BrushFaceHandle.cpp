#include "BrushFaceHandle.h"

#include "../../level/BrushGeometry.h"
#include "../../level/interactions/BrushResizeDragInteraction.h"
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

BrushFaceHandle::BrushFaceHandle(SelectableRef face, LevelScene& scene, LevelDocument& document)
    : Face(face)
    , Scene(scene)
    , Document(document)
{
}

HandleHit BrushFaceHandle::HitTest(const EditorViewport& viewport, ImVec2 screenPos) const
{
    if (viewport.Camera.ActiveMode != EditorCamera::Mode::Orthographic)
        return {};

    const std::optional<BrushFaceDescriptor> face = BrushGeometry::TryGetFace(Scene, Face, kHandleThickness);
    if (!face.has_value())
        return {};

    const Ray3d ray = BuildRay(viewport, screenPos);
    if (std::abs(face->Geometry.Normal.Dot(ray.Direction)) > 0.99f)
        return {};

    float dist = 0.0f;
    if (!RayIntersectsAabb(ray, face->Geometry.Bounds, dist))
        return {};

    return HandleHit{ .Hit = true, .Distance = dist };
}

std::unique_ptr<IInteraction> BrushFaceHandle::BeginDrag(ToolContext& /*ctx*/,
                                                         const EditorViewport& /*viewport*/,
                                                         ImVec2 /*screenPos*/) const
{
    const std::optional<BrushState> state = BrushGeometry::TryGetState(Scene, Face.Entity);
    if (!state.has_value())
        return nullptr;

    return std::make_unique<BrushResizeDragInteraction>(
        Face, *state, Scene, Document);
}
