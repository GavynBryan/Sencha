#include "TranslateGizmo.h"

#include "../tools/ToolContext.h"
#include "../viewport/EditorViewport.h"

#include <math/geometry/3d/Ray3d.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace
{
constexpr double kParallelEps = 1.0e-8;
constexpr float kAxisHitPixels = 12.0f;

// Axis part ids exchanged through IGizmo::HitTest/BeginDrag (0 == miss).
constexpr int kAxisX = 1;
constexpr int kAxisY = 2;
constexpr int kAxisZ = 3;

Ray3d BuildRay(const EditorViewport& viewport, ImVec2 point)
{
    const float w = viewport.RegionMax.x - viewport.RegionMin.x;
    const float h = viewport.RegionMax.y - viewport.RegionMin.y;
    if (w <= 0.0f || h <= 0.0f)
        return {};

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
    const Vec3d target(farW.X, farW.Y, farW.Z);
    return Ray3d(origin, (target - origin).Normalized());
}

std::optional<ImVec2> ProjectToScreen(const Mat4& viewProjection,
                                      const EditorViewport& viewport,
                                      Vec3d world)
{
    const Vec4 clip = viewProjection * Vec4(static_cast<float>(world.X),
                                            static_cast<float>(world.Y),
                                            static_cast<float>(world.Z),
                                            1.0f);
    if (clip.W <= kParallelEps)
        return std::nullopt;

    const float ndcX = clip.X / clip.W;
    const float ndcY = clip.Y / clip.W;
    const float width = viewport.RegionMax.x - viewport.RegionMin.x;
    const float height = viewport.RegionMax.y - viewport.RegionMin.y;
    return ImVec2{
        viewport.RegionMin.x + (ndcX * 0.5f + 0.5f) * width,
        viewport.RegionMin.y + (ndcY * 0.5f + 0.5f) * height,
    };
}

float DistancePointToSegment(ImVec2 p, ImVec2 a, ImVec2 b)
{
    const float abx = b.x - a.x;
    const float aby = b.y - a.y;
    const float lengthSq = abx * abx + aby * aby;
    float t = 0.0f;
    if (lengthSq > 0.0f)
        t = std::clamp(((p.x - a.x) * abx + (p.y - a.y) * aby) / lengthSq, 0.0f, 1.0f);
    const float dx = p.x - (a.x + t * abx);
    const float dy = p.y - (a.y + t * aby);
    return std::sqrt(dx * dx + dy * dy);
}

// Parameter s along the axis line (pivot + s*axisDir) of the point closest to the
// camera ray. nullopt when axis and ray are near-parallel (ill-conditioned).
// w0 runs pivot->origin so s carries the axis's own sign (a positive drag along
// +axis yields positive s); the opposite convention inverts the drag.
std::optional<double> ClosestAxisParam(Vec3d pivot, Vec3d axisDir, const Ray3d& ray)
{
    const Vec3d w0 = pivot - ray.Origin;
    const double b = axisDir.Dot(ray.Direction);
    const double denom = 1.0 - b * b;
    if (std::abs(denom) < kParallelEps)
        return std::nullopt;

    const double d = axisDir.Dot(w0);
    const double e = ray.Direction.Dot(w0);
    return (b * e - d) / denom;
}

// Absolute snap: returns the offset that lands the pivot on the nearest grid line
// along the axis (measured from the grid origin), so geometry snaps to grid
// positions, not just to grid-sized steps.
double SnapAxisOffset(double rawOffset, double pivotCoord, double originCoord, float spacing)
{
    if (spacing <= 0.0f)
        return rawOffset;
    const double target = pivotCoord + rawOffset;
    const double snapped = originCoord + std::round((target - originCoord) / spacing) * spacing;
    return snapped - pivotCoord;
}

// Two orthonormal vectors perpendicular to a unit axis (for arrowhead geometry).
void PerpendicularBasis(Vec3d axis, Vec3d& outU, Vec3d& outV)
{
    const Vec3d reference = std::abs(axis.Y) < 0.99f ? Vec3d(0.0f, 1.0f, 0.0f)
                                                     : Vec3d(1.0f, 0.0f, 0.0f);
    outU = axis.Cross(reference).Normalized();
    outV = axis.Cross(outU).Normalized();
}

// Drives an IGizmoHandler with the grid-snapped, axis-constrained translation.
class GizmoDragInteraction : public IInteraction
{
public:
    GizmoDragInteraction(Vec3d pivot, Vec3d axisDir, double startParam,
                         std::unique_ptr<IGizmoHandler> handler)
        : Pivot(pivot)
        , AxisDir(axisDir)
        , StartParam(startParam)
        , Handler(std::move(handler))
    {
    }

    void OnPointerMove(ToolContext&, EditorViewport& viewport, ImVec2 pos, ImVec2) override
    {
        if (UpdateDelta(viewport, pos))
            Handler->Preview(LastDelta);
    }

    void OnPointerUp(ToolContext&, EditorViewport& viewport, ImVec2 pos) override
    {
        UpdateDelta(viewport, pos);
        Handler->Commit(LastDelta);
    }

    void OnCancel(ToolContext&) override
    {
        Handler->Cancel();
    }

private:
    // Recomputes the grid-snapped world translation for the current cursor;
    // returns false (leaving LastDelta unchanged) when the axis is edge-on.
    bool UpdateDelta(const EditorViewport& viewport, ImVec2 pos)
    {
        const std::optional<double> s = ClosestAxisParam(Pivot, AxisDir, BuildRay(viewport, pos));
        if (!s.has_value())
            return false;
        const GridPlane grid = viewport.GetGrid();
        const double offset = SnapAxisOffset(
            *s - StartParam, Pivot.Dot(AxisDir), grid.Origin.Dot(AxisDir), grid.Spacing);
        LastDelta.Translation = AxisDir * static_cast<float>(offset);
        return true;
    }

    Vec3d Pivot;
    Vec3d AxisDir;
    double StartParam;
    GizmoDelta LastDelta = {};
    std::unique_ptr<IGizmoHandler> Handler;
};
}

void TranslateGizmo::SetPivot(Vec3d pivot)
{
    Pivot = pivot;
    HasPivot_ = true;
}

void TranslateGizmo::ClearPivot()
{
    HasPivot_ = false;
}

bool TranslateGizmo::HasPivot() const
{
    return HasPivot_;
}

Vec3d TranslateGizmo::GetPivot() const
{
    return Pivot;
}

Vec3d TranslateGizmo::AxisDirection(int axis)
{
    switch (axis)
    {
    case kAxisX: return Vec3d(1.0f, 0.0f, 0.0f);
    case kAxisY: return Vec3d(0.0f, 1.0f, 0.0f);
    case kAxisZ: return Vec3d(0.0f, 0.0f, 1.0f);
    default:     return Vec3d(0.0f, 0.0f, 0.0f);
    }
}

float TranslateGizmo::AxisLength(const EditorViewport& viewport, Vec3d pivot)
{
    const EditorCamera& camera = viewport.Camera;
    if (camera.ActiveMode == EditorCamera::Mode::Orthographic)
        return camera.OrthoHeight * 0.08f;

    const double distance = std::sqrt((camera.Position - pivot).SqrMagnitude());
    return static_cast<float>(std::max(0.5, distance * 0.18));
}

int TranslateGizmo::HitTest(const EditorViewport& viewport, ImVec2 screenPos) const
{
    if (!HasPivot_)
        return 0;

    const Mat4 viewProjection = viewport.BuildRenderData().ViewProjection;
    const std::optional<ImVec2> origin = ProjectToScreen(viewProjection, viewport, Pivot);
    if (!origin.has_value())
        return 0;

    const float length = AxisLength(viewport, Pivot);

    int best = 0;
    float bestPixels = kAxisHitPixels;
    for (const int axis : { kAxisX, kAxisY, kAxisZ })
    {
        const Vec3d end = Pivot + AxisDirection(axis) * length;
        const std::optional<ImVec2> endScreen = ProjectToScreen(viewProjection, viewport, end);
        if (!endScreen.has_value())
            continue;

        const float pixels = DistancePointToSegment(screenPos, *origin, *endScreen);
        if (pixels <= bestPixels)
        {
            bestPixels = pixels;
            best = axis;
        }
    }

    return best;
}

std::unique_ptr<IInteraction> TranslateGizmo::BeginDrag(
    int part,
    const EditorViewport& viewport,
    ImVec2 screenPos,
    std::unique_ptr<IGizmoHandler> handler) const
{
    const Vec3d axisDir = AxisDirection(part);
    if (!HasPivot_ || part == 0 || handler == nullptr || axisDir.SqrMagnitude() == 0.0f)
        return nullptr;

    const std::optional<double> startParam =
        ClosestAxisParam(Pivot, axisDir, BuildRay(viewport, screenPos));
    if (!startParam.has_value())
        return nullptr;

    return std::make_unique<GizmoDragInteraction>(Pivot, axisDir, *startParam, std::move(handler));
}

void TranslateGizmo::AppendGeometry(const EditorViewport& viewport,
                                    std::vector<GizmoLine>& out) const
{
    if (!HasPivot_)
        return;

    const float length = AxisLength(viewport, Pivot);
    const float headLength = length * 0.18f;
    const float headRadius = headLength * 0.45f;

    const std::array<int, 3> axes = { kAxisX, kAxisY, kAxisZ };
    const std::array<Vec4, 3> colors = {
        Vec4(1.0f, 0.2f, 0.2f, 1.0f), // X
        Vec4(0.2f, 1.0f, 0.2f, 1.0f), // Y
        Vec4(0.3f, 0.4f, 1.0f, 1.0f), // Z
    };

    for (std::size_t i = 0; i < axes.size(); ++i)
    {
        const Vec3d dir = AxisDirection(axes[i]);
        const Vec4 color = colors[i];
        const Vec3d tip = Pivot + dir * length;
        const Vec3d base = tip - dir * headLength;

        // Shaft.
        out.push_back(GizmoLine{ .A = Pivot, .B = tip, .Color = color });

        // Arrowhead: four lines from the tip to a small ring, plus the ring edges.
        Vec3d u;
        Vec3d v;
        PerpendicularBasis(dir, u, v);
        const std::array<Vec3d, 4> ring = {
            base + u * headRadius,
            base + v * headRadius,
            base - u * headRadius,
            base - v * headRadius,
        };
        for (std::size_t c = 0; c < ring.size(); ++c)
        {
            out.push_back(GizmoLine{ .A = tip, .B = ring[c], .Color = color });
            out.push_back(GizmoLine{ .A = ring[c], .B = ring[(c + 1) % ring.size()], .Color = color });
        }
    }
}
