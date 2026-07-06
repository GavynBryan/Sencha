#include "BoundsManipulator.h"

#include "GizmoMath.h"
#include "EditorTheme.h"
#include "brush/BrushBounds.h"
#include "meshedit/ManipulationSink.h"
#include "meshedit/MeshEditService.h"
#include "tools/ToolContext.h"
#include "viewport/EditorViewport.h"
#include "viewport/ViewportProjection.h"

#include <math/spatial/GridPlane.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <optional>
#include <vector>

namespace
{
constexpr float kHandleHitPixels = 11.0f;
constexpr float kBallPixels = 7.0f;   // screen-constant handle ball size
constexpr float kDashPixels = 7.0f;   // dash period along the axis stalks
constexpr double kMinThickness = 0.05;

double AxisGet(const Vec3d& v, int a) { return a == 0 ? v.X : (a == 1 ? v.Y : v.Z); }
void AxisSet(Vec3d& v, int a, double x) { (a == 0 ? v.X : (a == 1 ? v.Y : v.Z)) = static_cast<float>(x); }

Vec4 AxisColor(int a) { return a == 0 ? EditorTheme::AxisX : (a == 1 ? EditorTheme::AxisY : EditorTheme::AxisZ); }

// The box lives in the gizmo frame (ctx.Axes: world, grid, or local space):
// coordinates are dot products with the frame axes, world points reconstruct as
// the axis-weighted sum (the axes are orthonormal). The world frame reproduces
// the historical world-AABB behavior exactly.
using Frame = std::array<Vec3d, 3>;

Vec3d FrameCoords(const Frame& axes, Vec3d world)
{
    return Vec3d(world.Dot(axes[0]), world.Dot(axes[1]), world.Dot(axes[2]));
}

Vec3d FramePoint(const Frame& axes, Vec3d coords)
{
    return axes[0] * coords.X + axes[1] * coords.Y + axes[2] * coords.Z;
}

EntityId PickObjectEntity(const SelectionSnapshot& selection)
{
    if (selection.Primary.IsEntity())
        return selection.Primary.Entity;
    for (SelectableRef ref : selection.Items)
        if (ref.IsEntity())
            return ref.Entity;
    return {};
}

// One face-center handle: which frame axis it drives and which side (min/max).
// Center is in FRAME coordinates; map through FramePoint for world.
struct FaceHandle
{
    Vec3d Center;
    int Axis = 0;
    bool IsMax = false;
};

std::array<FaceHandle, 6> BuildHandles(Vec3d mn, Vec3d mx)
{
    const Vec3d mid = (mn + mx) * 0.5f;
    const auto face = [&](int axis, bool isMax) {
        Vec3d c = mid;
        AxisSet(c, axis, isMax ? AxisGet(mx, axis) : AxisGet(mn, axis));
        return FaceHandle{ c, axis, isMax };
    };
    return { face(0, true), face(0, false), face(1, true), face(1, false), face(2, true), face(2, false) };
}

void AppendBox(ManipulatorVisual& out, Vec3d center, float half, const Vec4& color)
{
    const auto corner = [&](int sx, int sy, int sz) {
        return center + Vec3d(static_cast<float>(sx) * half, static_cast<float>(sy) * half, static_cast<float>(sz) * half);
    };
    const Vec3d c[8] = {
        corner(-1, -1, -1), corner(1, -1, -1), corner(1, 1, -1), corner(-1, 1, -1),
        corner(-1, -1, 1),  corner(1, -1, 1),  corner(1, 1, 1),  corner(-1, 1, 1),
    };
    const int edges[12][2] = { {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7} };
    for (const auto& e : edges)
        out.Lines.push_back({ .A = c[e[0]], .B = c[e[1]], .Color = color });
}

// Dashed segment: emit only the "on" pieces of a/b as short solid lines.
void AppendDashed(ManipulatorVisual& out, Vec3d a, Vec3d b, const Vec4& color, float dash)
{
    const Vec3d delta = b - a;
    const float length = delta.Magnitude();
    if (length < 1.0e-5f || dash <= 0.0f)
        return;
    const Vec3d dir = delta * (1.0f / length);
    bool on = true;
    for (float t = 0.0f; t < length; t += dash)
    {
        if (on)
        {
            const float end = std::min(length, t + dash);
            out.Lines.push_back({ .A = a + dir * t, .B = a + dir * end, .Color = color });
        }
        on = !on;
    }
}

// Resolves the single selected brush in object mode and its oriented bounds in
// the gizmo frame (Min/Max are frame coordinates). Applies in any view.
// Returns false when the manipulator does not apply.
struct Resolved
{
    EntityId Entity;
    BrushMesh Mesh;
    Transform3f Transform;
    Vec3d Min;
    Vec3d Max;
};

bool Resolve(const ManipulatorContext& ctx, Resolved& out)
{
    if (ctx.Service.GetElementKind() != MeshElementKind::Object)
        return false;

    const EntityId entity = PickObjectEntity(ctx.Selection);
    if (!entity.IsValid())
        return false;

    const std::optional<MeshEditTargetMesh> resolved = ctx.Sink.ResolveMesh(entity);
    if (!resolved.has_value() || resolved->Mesh == nullptr || resolved->Mesh->Vertices.empty())
        return false;

    Vec3d mn(FLT_MAX, FLT_MAX, FLT_MAX);
    Vec3d mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (const BrushVertex& vertex : resolved->Mesh->Vertices)
    {
        const Vec3d c = FrameCoords(ctx.Axes, resolved->Transform.TransformPoint(vertex.Position));
        mn.X = std::min(mn.X, c.X); mn.Y = std::min(mn.Y, c.Y); mn.Z = std::min(mn.Z, c.Z);
        mx.X = std::max(mx.X, c.X); mx.Y = std::max(mx.Y, c.Y); mx.Z = std::max(mx.Z, c.Z);
    }
    if (!(mn.X <= mx.X && mn.Y <= mx.Y && mn.Z <= mx.Z))
        return false;

    out.Entity = entity;
    out.Mesh = *resolved->Mesh;
    out.Transform = resolved->Transform;
    out.Min = mn;
    out.Max = mx;
    return true;
}

// Drags one face handle along its frame axis, resizing that side (grid-snapped,
// min-thickness clamped) about the opposite side via MeshEditService::ResizeBounds.
class BoundsDrag : public IInteraction
{
public:
    BoundsDrag(EntityId entity, BrushMesh base, Transform3f transform, Frame axes,
               Vec3d mn, Vec3d mx, FaceHandle handle, MeshEditService& service, ManipulationSink& sink)
        : Entity(entity), Base(std::move(base)), Transform(transform), Axes(axes)
        , OldMin(mn), OldMax(mx), H(handle), Service(service), Sink(sink) {}

    void OnPointerMove(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override
    {
        if (const std::optional<BrushMesh> mesh = Compute(ctx, viewport, pointer, false))
            Sink.PreviewMesh(Entity, *mesh);
    }

    void OnPointerUp(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override
    {
        if (const std::optional<BrushMesh> mesh = Compute(ctx, viewport, pointer, true))
            Sink.CommitMesh(Entity, Base, *mesh);
        else
            Sink.PreviewMesh(Entity, Base); // unusable result: revert
    }

    void OnCancel(ToolContext&) override { Sink.PreviewMesh(Entity, Base); }

private:
    std::optional<BrushMesh> Compute(ToolContext& ctx, EditorViewport& viewport,
                                     const PointerEvent& pointer, bool validate) const
    {
        const Ray3d ray = ViewportProjection(viewport).RayThroughPixel(pointer.Position);
        const Vec3d axisDir = Axes[static_cast<std::size_t>(H.Axis)];
        const std::optional<double> s =
            GizmoMath::ClosestAxisParam(FramePoint(Axes, H.Center), axisDir, ray);
        if (!s.has_value())
            return std::nullopt;

        const double pivot = AxisGet(H.Center, H.Axis);
        double coord = pivot + *s;
        const GridPlane grid = viewport.GetGrid(ctx.Grid);
        if (grid.SnapEnabled && grid.Spacing > 0.0f)
        {
            // Absolute snap through the shared kernel, in frame coordinates
            // (the lattice anchor is the grid origin's coordinate on this axis).
            const double origin = grid.Origin.Dot(axisDir);
            coord = pivot + GizmoMath::SnapAxisOffset(*s, pivot, origin, grid.Spacing);
        }

        Vec3d newMin = OldMin;
        Vec3d newMax = OldMax;
        if (H.IsMax)
            AxisSet(newMax, H.Axis, std::max(coord, AxisGet(OldMin, H.Axis) + kMinThickness));
        else
            AxisSet(newMin, H.Axis, std::min(coord, AxisGet(OldMax, H.Axis) - kMinThickness));

        return Service.ResizeBounds(Base, Transform, Axes, OldMin, OldMax, newMin, newMax, validate);
    }

    EntityId Entity;
    BrushMesh Base;
    Transform3f Transform;
    Frame Axes;
    Vec3d OldMin;
    Vec3d OldMax;
    FaceHandle H;
    MeshEditService& Service;
    ManipulationSink& Sink;
};
}

bool BoundsManipulator::AppliesTo(const ManipulatorContext& ctx, const EditorViewport&) const
{
    Resolved r;
    return Resolve(ctx, r);
}

void BoundsManipulator::BuildVisual(const ManipulatorContext& ctx,
                                    const EditorViewport& viewport,
                                    int hoveredPart,
                                    ManipulatorVisual& out) const
{
    Resolved r;
    if (!Resolve(ctx, r))
        return;

    // Faint oriented box edges (frame corners mapped to world).
    const auto corner = [&](bool xMax, bool yMax, bool zMax) {
        return FramePoint(ctx.Axes,
                          Vec3d(xMax ? r.Max.X : r.Min.X, yMax ? r.Max.Y : r.Min.Y, zMax ? r.Max.Z : r.Min.Z));
    };
    const Vec3d c[8] = {
        corner(false, false, false), corner(true, false, false),
        corner(true, true, false),   corner(false, true, false),
        corner(false, false, true),  corner(true, false, true),
        corner(true, true, true),    corner(false, true, true),
    };
    const int edges[12][2] = { {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7} };
    for (const auto& e : edges)
        out.Lines.push_back({ .A = c[e[0]], .B = c[e[1]], .Color = EditorTheme::BoundsBox });

    const ViewportProjection projection(viewport);
    const Vec3d center = FramePoint(ctx.Axes, (r.Min + r.Max) * 0.5f);
    const float dash = projection.WorldSizeForPixels(center, kDashPixels);

    // Center marker, then a dotted axis to each face handle with a ball at its end.
    AppendBox(out, center, projection.WorldSizeForPixels(center, kBallPixels) * 0.4f, EditorTheme::Handle);
    const std::array<FaceHandle, 6> handles = BuildHandles(r.Min, r.Max);
    for (std::size_t i = 0; i < handles.size(); ++i)
    {
        const FaceHandle& h = handles[i];
        const Vec3d handleWorld = FramePoint(ctx.Axes, h.Center);
        const Vec4 color = (static_cast<int>(i) + 1 == hoveredPart) ? EditorTheme::Hover : AxisColor(h.Axis);
        AppendDashed(out, center, handleWorld, color, dash);
        AppendBox(out, handleWorld, projection.WorldSizeForPixels(handleWorld, kBallPixels) * 0.5f, color);
    }
}

int BoundsManipulator::HitTest(const ManipulatorContext& ctx,
                               const EditorViewport& viewport,
                               ImVec2 screenPos) const
{
    Resolved r;
    if (!Resolve(ctx, r))
        return 0;

    const ViewportProjection projection(viewport);
    const std::array<FaceHandle, 6> handles = BuildHandles(r.Min, r.Max);

    int best = 0;
    float bestPixels = kHandleHitPixels;
    for (std::size_t i = 0; i < handles.size(); ++i)
    {
        const std::optional<ProjectedPoint> p = projection.WorldToPixel(FramePoint(ctx.Axes, handles[i].Center));
        if (!p.has_value())
            continue;
        const float dx = screenPos.x - p->Pixel.x;
        const float dy = screenPos.y - p->Pixel.y;
        const float pixels = std::sqrt(dx * dx + dy * dy);
        if (pixels <= bestPixels)
        {
            bestPixels = pixels;
            best = static_cast<int>(i) + 1;
        }
    }
    return best;
}

std::unique_ptr<IInteraction> BoundsManipulator::BeginDrag(
    int part,
    const ManipulatorContext& ctx,
    const EditorViewport&,
    ImVec2,
    ModifierFlags) const
{
    Resolved r;
    if (!Resolve(ctx, r))
        return nullptr;

    const std::array<FaceHandle, 6> handles = BuildHandles(r.Min, r.Max);
    if (part <= 0 || static_cast<std::size_t>(part) > handles.size())
        return nullptr;

    return std::make_unique<BoundsDrag>(
        r.Entity, std::move(r.Mesh), r.Transform, ctx.Axes, r.Min, r.Max,
        handles[static_cast<std::size_t>(part - 1)], ctx.Service, ctx.Sink);
}
