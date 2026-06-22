#include "BoundsManipulator.h"

#include "../EditorTheme.h"
#include "../level/brush/BrushBounds.h"
#include "../meshedit/ManipulationSink.h"
#include "../meshedit/MeshEditService.h"
#include "../tools/ToolContext.h"
#include "../viewport/EditorViewport.h"
#include "../viewport/Picking.h"
#include "../viewport/ViewportProjection.h"

#include <math/spatial/GridPlane.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace
{
constexpr float kHandleHitPixels = 11.0f;
constexpr double kMinThickness = 0.05;

double AxisGet(const Vec3d& v, int a) { return a == 0 ? v.X : (a == 1 ? v.Y : v.Z); }
void AxisSet(Vec3d& v, int a, double x) { (a == 0 ? v.X : (a == 1 ? v.Y : v.Z)) = static_cast<float>(x); }

int AxisIndex(Vec3d unitAxis)
{
    const double ax = std::abs(unitAxis.X);
    const double ay = std::abs(unitAxis.Y);
    const double az = std::abs(unitAxis.Z);
    if (ax >= ay && ax >= az) return 0;
    return ay >= az ? 1 : 2;
}

bool IsOrtho(const EditorViewport& viewport)
{
    return viewport.Camera.ActiveMode == EditorCamera::Mode::Orthographic;
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

// One draggable handle: its world centre plus which AABB sides it drives.
struct Handle
{
    Vec3d Center;
    bool MovesU = false;
    bool UMax = false;
    bool MovesV = false;
    bool VMax = false;
};

// 4 corners + 4 edge-midpoints of the box's in-view (U,V) rectangle, drawn at the
// box's mid-depth on the perpendicular (W) axis.
std::vector<Handle> BuildHandles(Vec3d mn, Vec3d mx, int uAxis, int vAxis, int wAxis)
{
    const double wMid = (AxisGet(mn, wAxis) + AxisGet(mx, wAxis)) * 0.5;
    const double uMid = (AxisGet(mn, uAxis) + AxisGet(mx, uAxis)) * 0.5;
    const double vMid = (AxisGet(mn, vAxis) + AxisGet(mx, vAxis)) * 0.5;

    const auto make = [&](double u, double v, bool movesU, bool uMax, bool movesV, bool vMax) {
        Handle h;
        AxisSet(h.Center, uAxis, u);
        AxisSet(h.Center, vAxis, v);
        AxisSet(h.Center, wAxis, wMid);
        h.MovesU = movesU; h.UMax = uMax; h.MovesV = movesV; h.VMax = vMax;
        return h;
    };

    const double u0 = AxisGet(mn, uAxis), u1 = AxisGet(mx, uAxis);
    const double v0 = AxisGet(mn, vAxis), v1 = AxisGet(mx, vAxis);
    return {
        make(u0, v0, true, false, true, false), // corners
        make(u1, v0, true, true,  true, false),
        make(u1, v1, true, true,  true, true),
        make(u0, v1, true, false, true, true),
        make(u0, vMid, true, false, false, false), // edge mids (one axis each)
        make(u1, vMid, true, true,  false, false),
        make(uMid, v0, false, false, true, false),
        make(uMid, v1, false, false, true, true),
    };
}

// Resolves the single selected brush in object mode and its world AABB + the
// view's world axis indices. Returns false when the manipulator does not apply.
struct Resolved
{
    EntityId Entity;
    const BrushMesh* Mesh = nullptr; // points into the scene; valid for the call
    Transform3f Transform;
    Vec3d Min;
    Vec3d Max;
    int UAxis = 0;
    int VAxis = 2;
    int WAxis = 1;
};

bool Resolve(const ManipulatorContext& ctx, const EditorViewport& viewport, Resolved& out)
{
    if (!IsOrtho(viewport) || ctx.Service.GetElementKind() != MeshElementKind::Object)
        return false;

    const EntityId entity = PickObjectEntity(ctx.Selection);
    if (!entity.IsValid())
        return false;

    const std::optional<MeshEditTargetMesh> resolved = ctx.Sink.ResolveMesh(entity);
    if (!resolved.has_value() || resolved->Mesh == nullptr || resolved->Mesh->Vertices.empty())
        return false;

    out.Entity = entity;
    out.Mesh = resolved->Mesh;
    out.Transform = resolved->Transform;

    const Aabb3d bounds = BrushWorldBounds(*out.Mesh, out.Transform);
    out.Min = bounds.Min;
    out.Max = bounds.Max;

    const GridPlane grid = viewport.GetGrid(ctx.Grid);
    out.UAxis = AxisIndex(grid.AxisU);
    out.VAxis = AxisIndex(grid.AxisV);
    out.WAxis = 3 - out.UAxis - out.VAxis;
    return true;
}

// Drags one bounds handle: each move recomputes the box and previews; commit
// validates; cancel reverts.
class BoundsDrag : public IInteraction
{
public:
    BoundsDrag(EntityId entity, BrushMesh base, Transform3f transform,
               Vec3d mn, Vec3d mx, int uAxis, int vAxis, Handle handle,
               MeshEditService& service, ManipulationSink& sink)
        : Entity(entity), Base(std::move(base)), Transform(transform)
        , OldMin(mn), OldMax(mx), UAxis(uAxis), VAxis(vAxis), H(handle)
        , Service(service), Sink(sink) {}

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
        const std::optional<Vec3d> grid = ctx.Picking.ProjectPointToGrid(viewport, pointer.Position, ctx.Grid);
        if (!grid.has_value())
            return std::nullopt;

        Vec3d newMin = OldMin;
        Vec3d newMax = OldMax;
        if (H.MovesU)
        {
            AxisSet(H.UMax ? newMax : newMin, UAxis, AxisGet(*grid, UAxis));
            ClampAxis(newMin, newMax, UAxis, H.UMax);
        }
        if (H.MovesV)
        {
            AxisSet(H.VMax ? newMax : newMin, VAxis, AxisGet(*grid, VAxis));
            ClampAxis(newMin, newMax, VAxis, H.VMax);
        }
        return Service.ResizeBounds(Base, Transform, OldMin, OldMax, newMin, newMax, validate);
    }

    static void ClampAxis(Vec3d& mn, Vec3d& mx, int axis, bool movedMax)
    {
        if (movedMax)
            AxisSet(mx, axis, std::max(AxisGet(mx, axis), AxisGet(mn, axis) + kMinThickness));
        else
            AxisSet(mn, axis, std::min(AxisGet(mn, axis), AxisGet(mx, axis) - kMinThickness));
    }

    EntityId Entity;
    BrushMesh Base;
    Transform3f Transform;
    Vec3d OldMin;
    Vec3d OldMax;
    int UAxis;
    int VAxis;
    Handle H;
    MeshEditService& Service;
    ManipulationSink& Sink;
};
}

bool BoundsManipulator::AppliesTo(const ManipulatorContext& ctx, const EditorViewport& viewport) const
{
    Resolved r;
    return Resolve(ctx, viewport, r);
}

void BoundsManipulator::BuildVisual(const ManipulatorContext& ctx,
                                    const EditorViewport& viewport,
                                    int hoveredPart,
                                    ManipulatorVisual& out) const
{
    Resolved r;
    if (!Resolve(ctx, viewport, r))
        return;

    const GridPlane grid = viewport.GetGrid(ctx.Grid);
    const Vec3d uVec = grid.AxisU;
    const Vec3d vVec = grid.AxisV;
    const Vec4 boxColor = EditorTheme::BoundsBox;

    // Box edges (12) of the world AABB.
    const auto corner = [&](bool xMax, bool yMax, bool zMax) {
        return Vec3d(xMax ? r.Max.X : r.Min.X, yMax ? r.Max.Y : r.Min.Y, zMax ? r.Max.Z : r.Min.Z);
    };
    const Vec3d c[8] = {
        corner(false, false, false), corner(true, false, false),
        corner(true, true, false),   corner(false, true, false),
        corner(false, false, true),  corner(true, false, true),
        corner(true, true, true),    corner(false, true, true),
    };
    const int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7},
    };
    for (const auto& e : edges)
        out.Lines.push_back({ .A = c[e[0]], .B = c[e[1]], .Color = boxColor });

    // Handles: screen-constant squares in the view plane; the hovered one warms.
    const ViewportProjection projection(viewport);
    const std::vector<Handle> handles = BuildHandles(r.Min, r.Max, r.UAxis, r.VAxis, r.WAxis);
    for (std::size_t i = 0; i < handles.size(); ++i)
    {
        const Handle& h = handles[i];
        const Vec4 color = (static_cast<int>(i) + 1 == hoveredPart) ? EditorTheme::Hover : EditorTheme::Handle;
        const float half = projection.WorldSizeForPixels(h.Center, EditorTheme::HandlePixels) * 0.5f;
        const Vec3d a = h.Center + uVec * half + vVec * half;
        const Vec3d b = h.Center - uVec * half + vVec * half;
        const Vec3d cc = h.Center - uVec * half - vVec * half;
        const Vec3d d = h.Center + uVec * half - vVec * half;
        out.Lines.push_back({ .A = a, .B = b, .Color = color });
        out.Lines.push_back({ .A = b, .B = cc, .Color = color });
        out.Lines.push_back({ .A = cc, .B = d, .Color = color });
        out.Lines.push_back({ .A = d, .B = a, .Color = color });
    }
}

int BoundsManipulator::HitTest(const ManipulatorContext& ctx,
                               const EditorViewport& viewport,
                               ImVec2 screenPos) const
{
    Resolved r;
    if (!Resolve(ctx, viewport, r))
        return 0;

    const ViewportProjection projection(viewport);
    const std::vector<Handle> handles = BuildHandles(r.Min, r.Max, r.UAxis, r.VAxis, r.WAxis);

    int best = 0;
    float bestPixels = kHandleHitPixels;
    for (std::size_t i = 0; i < handles.size(); ++i)
    {
        const std::optional<ProjectedPoint> p = projection.WorldToPixel(handles[i].Center);
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
    const EditorViewport& viewport,
    ImVec2,
    ModifierFlags) const
{
    Resolved r;
    if (!Resolve(ctx, viewport, r))
        return nullptr;

    const std::vector<Handle> handles = BuildHandles(r.Min, r.Max, r.UAxis, r.VAxis, r.WAxis);
    if (part <= 0 || static_cast<std::size_t>(part) > handles.size())
        return nullptr;

    return std::make_unique<BoundsDrag>(
        r.Entity, *r.Mesh, r.Transform, r.Min, r.Max, r.UAxis, r.VAxis,
        handles[static_cast<std::size_t>(part - 1)], ctx.Service, ctx.Sink);
}
