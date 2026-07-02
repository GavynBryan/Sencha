#include "RotateManipulator.h"

#include "ManipulatorTargets.h"
#include "../meshedit/MeshElements.h"
#include "SelectionPivot.h"
#include "../EditorTheme.h"
#include "../meshedit/ManipulationSink.h"
#include "../meshedit/MeshEditService.h"
#include "../overlay/EditorOverlayState.h"
#include "../tools/ToolContext.h"
#include "../viewport/EditorViewport.h"
#include "../viewport/ViewportProjection.h"

#include <math/Quat.h>
#include <math/geometry/3d/Ray3d.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <optional>
#include <utility>
#include <vector>

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr float kRingPixels = 80.0f;     // screen-constant ring radius
constexpr float kRingHitPixels = 8.0f;   // cursor-to-ring tolerance
constexpr int kRingSegments = 48;
constexpr double kSnapDegrees = 15.0;     // rotation snap step when grid snap is on

// Ring axes come from the session-resolved gizmo frame (world, grid, or local
// space); the part id indexes into it.
Vec3d AxisDirection(const ManipulatorContext& ctx, int part)
{
    if (part < 1 || part > 3)
        return Vec3d(0.0f, 0.0f, 0.0f);
    return ctx.Axes[static_cast<std::size_t>(part - 1)];
}

// The two in-plane unit axes for a ring, ordered so u x v == the rotation axis
// (right-hand rule, so a positive drag angle is a positive rotation). The frame
// axes are orthonormal, so the other two frame axes span each ring's plane.
void RingBasis(const ManipulatorContext& ctx, int part, Vec3d& u, Vec3d& v)
{
    switch (part)
    {
    case 1: u = ctx.Axes[1]; v = ctx.Axes[2]; break; // about frame X
    case 2: u = ctx.Axes[2]; v = ctx.Axes[0]; break; // about frame Y
    default: u = ctx.Axes[0]; v = ctx.Axes[1]; break; // about frame Z
    }
}

float RingRadius(const EditorViewport& viewport, Vec3d pivot)
{
    return ViewportProjection(viewport).WorldSizeForPixels(pivot, kRingPixels);
}

std::optional<Vec3d> RayPlaneHit(const Ray3d& ray, Vec3d point, Vec3d normal)
{
    const double denom = ray.Direction.Dot(normal);
    if (std::abs(denom) < 1.0e-8)
        return std::nullopt;
    const double t = (point - ray.Origin).Dot(normal) / denom;
    return ray.Origin + ray.Direction * static_cast<float>(t);
}

// The point the rings sit on: the grid origin while grid editing, otherwise the
// selection pivot.
std::optional<Vec3d> GizmoPivot(const ManipulatorContext& ctx)
{
    if (ctx.EditGridOrigin)
        return ctx.Grid.Origin;
    return ComputeSelectionPivot(ctx.Sink, ctx.Selection, ctx.Service.GetElementKind(), ctx.Pivot);
}

// Applies a rotation (radians about a fixed world axis through a fixed pivot) to
// whatever is being manipulated, via the sink.
struct IRotateApply
{
    virtual void Preview(double radians) = 0;
    virtual void Commit(double radians) = 0;
    virtual void Cancel() = 0;
    virtual ~IRotateApply() = default;
};

Transform3f RotatedTransform(const Transform3f& initial, Vec3d axis, double radians, Vec3d pivot)
{
    const Quatf q = Quatf::FromAxisAngle(axis, static_cast<float>(radians));
    Transform3f result = initial;
    result.Position = pivot + q.RotateVector(initial.Position - pivot);
    result.Rotation = q * initial.Rotation; // apply q after the existing orientation
    return result;
}

class ObjectRotateApply : public IRotateApply
{
public:
    ObjectRotateApply(std::vector<ObjectTarget> items, Vec3d axis, Vec3d pivot, ManipulationSink& sink)
        : Items(std::move(items)), Axis(axis), Pivot(pivot), Sink(sink) {}

    void Preview(double radians) override
    {
        for (const ObjectTarget& item : Items)
            Sink.PreviewTransform(item.Entity, RotatedTransform(item.Initial, Axis, radians, Pivot));
    }

    void Commit(double radians) override
    {
        std::vector<TransformEdit> edits;
        edits.reserve(Items.size());
        for (const ObjectTarget& item : Items)
            edits.push_back({ item.Entity, item.Initial, RotatedTransform(item.Initial, Axis, radians, Pivot) });
        Sink.CommitTransforms(edits);
    }

    void Cancel() override
    {
        for (const ObjectTarget& item : Items)
            Sink.PreviewTransform(item.Entity, item.Initial);
    }

private:
    std::vector<ObjectTarget> Items;
    Vec3d Axis;
    Vec3d Pivot;
    ManipulationSink& Sink;
};

class ElementRotateApply : public IRotateApply
{
public:
    ElementRotateApply(EntityId entity, BrushMesh initial, Transform3f transform,
                       std::vector<SelectableRef> elements, MeshElementKind kind,
                       Vec3d axis, Vec3d pivot, MeshEditService& service, ManipulationSink& sink)
        : Entity(entity), Initial(std::move(initial)), Transform(transform)
        , Elements(std::move(elements)), Kind(kind), Axis(axis), Pivot(pivot)
        , Service(service), Sink(sink) {}

    void Preview(double radians) override
    {
        if (auto mesh = Service.RotateElements(Initial, Transform, Elements, Kind, Axis, radians, Pivot, false))
            Sink.PreviewMesh(Entity, *mesh);
    }

    void Commit(double radians) override
    {
        if (auto mesh = Service.RotateElements(Initial, Transform, Elements, Kind, Axis, radians, Pivot, true))
            Sink.CommitMesh(Entity, Initial, std::move(*mesh));
        else
            Sink.PreviewMesh(Entity, Initial); // unusable: revert
    }

    void Cancel() override { Sink.PreviewMesh(Entity, Initial); }

private:
    EntityId Entity;
    BrushMesh Initial;
    Transform3f Transform;
    std::vector<SelectableRef> Elements;
    MeshElementKind Kind;
    Vec3d Axis;
    Vec3d Pivot;
    MeshEditService& Service;
    ManipulationSink& Sink;
};

// Grid-edit: the ring drag rotates the workspace grid frame about the dragged
// axis (view state, no undo step); cancel restores the prior frame. The axes
// are re-orthonormalized each preview so repeated drags cannot drift.
class GridFrameRotateApply : public IRotateApply
{
public:
    GridFrameRotateApply(GridSettings& grid, Vec3d axis)
        : Grid(grid), Axis(axis), StartU(grid.AxisU), StartV(grid.AxisV) {}

    void Preview(double radians) override { Set(radians); }
    void Commit(double radians) override { Set(radians); }
    void Cancel() override
    {
        Grid.AxisU = StartU;
        Grid.AxisV = StartV;
    }

private:
    void Set(double radians)
    {
        const Quatf q = Quatf::FromAxisAngle(Axis, static_cast<float>(radians));
        Vec3d u = q.RotateVector(StartU).Normalized();
        Vec3d v = q.RotateVector(StartV);
        v = (v - u * v.Dot(u)).Normalized();
        Grid.AxisU = u;
        Grid.AxisV = v;
    }

    GridSettings& Grid;
    Vec3d Axis;
    Vec3d StartU;
    Vec3d StartV;
};

// Shift-drag on a ring in object mode: live copies follow the rotation, the
// originals stay put; release commits the copies as one undoable step. The
// rotate twin of the Move gizmo's duplicate drag.
class DuplicateRotateApply : public IRotateApply
{
public:
    DuplicateRotateApply(std::vector<ObjectTarget> items, Vec3d axis, Vec3d pivot, ManipulationSink& sink)
        : Items(std::move(items)), Axis(axis), Pivot(pivot), Sink(sink) {}

    void Preview(double radians) override
    {
        if (Previews.empty())
        {
            std::vector<EntityId> sources;
            sources.reserve(Items.size());
            for (const ObjectTarget& item : Items)
                sources.push_back(item.Entity);
            Previews = Sink.CreatePreviewDuplicates(sources);
        }
        for (std::size_t i = 0; i < Previews.size() && i < Items.size(); ++i)
            Sink.PreviewTransform(Previews[i], RotatedTransform(Items[i].Initial, Axis, radians, Pivot));
    }

    void Commit(double radians) override
    {
        Sink.DestroyPreviewEntities(Previews);
        Previews.clear();

        // A Shift-click with no real turn should not stamp an in-place copy.
        if (radians == 0.0)
            return;

        std::vector<EntityId> sources;
        std::vector<Transform3f> transforms;
        sources.reserve(Items.size());
        transforms.reserve(Items.size());
        for (const ObjectTarget& item : Items)
        {
            sources.push_back(item.Entity);
            transforms.push_back(RotatedTransform(item.Initial, Axis, radians, Pivot));
        }
        Sink.CommitDuplicate(sources, transforms);
    }

    void Cancel() override
    {
        Sink.DestroyPreviewEntities(Previews);
        Previews.clear();
    }

private:
    std::vector<ObjectTarget> Items;
    Vec3d Axis;
    Vec3d Pivot;
    ManipulationSink& Sink;
    std::vector<EntityId> Previews;
};

// Shift-drag on a ring in face mode: extrude the selection a step along its
// average normal, then swing the new cap by the drag angle. The extrude happens
// once up front (its NewElementIds are what the rotation drives); commit
// validates, cancel restores the original mesh.
class ExtrudeRotateApply : public IRotateApply
{
public:
    ExtrudeRotateApply(EntityId entity, BrushMesh initial,
                       MeshEditService::ExtrudeResult extruded, RegistryId registry,
                       Transform3f transform, MeshElementKind kind,
                       Vec3d axis, Vec3d pivot, MeshEditService& service, ManipulationSink& sink)
        : Entity(entity), Initial(std::move(initial)), Extruded(std::move(extruded))
        , Transform(transform), Kind(kind), Axis(axis), Pivot(pivot)
        , Service(service), Sink(sink)
    {
        NewRefs.reserve(Extruded.NewElementIds.size());
        for (std::uint32_t id : Extruded.NewElementIds)
            NewRefs.push_back(kind == MeshElementKind::Face
                                  ? SelectableRef::FaceSelection(registry, entity, id)
                                  : SelectableRef::EdgeSelection(registry, entity, id));
    }

    void Preview(double radians) override
    {
        if (auto mesh = Service.RotateElements(Extruded.Mesh, Transform, NewRefs, Kind, Axis, radians, Pivot, false))
            Sink.PreviewMesh(Entity, *mesh);
    }

    void Commit(double radians) override
    {
        if (auto mesh = Service.RotateElements(Extruded.Mesh, Transform, NewRefs, Kind, Axis, radians, Pivot, true))
            Sink.CommitMesh(Entity, Initial, std::move(*mesh));
        else
            Sink.PreviewMesh(Entity, Initial); // unusable: revert
    }

    void Cancel() override { Sink.PreviewMesh(Entity, Initial); }

private:
    EntityId Entity;
    BrushMesh Initial;
    MeshEditService::ExtrudeResult Extruded;
    Transform3f Transform;
    MeshElementKind Kind;
    Vec3d Axis;
    Vec3d Pivot;
    MeshEditService& Service;
    ManipulationSink& Sink;
    std::vector<SelectableRef> NewRefs;
};

std::unique_ptr<IRotateApply> MakeObjectRotateApply(const ManipulatorContext& ctx, Vec3d axis, Vec3d pivot)
{
    std::vector<ObjectTarget> items = GatherObjectTargets(ctx);
    if (items.empty())
        return nullptr;
    return std::make_unique<ObjectRotateApply>(std::move(items), axis, pivot, ctx.Sink);
}

std::unique_ptr<IRotateApply> MakeDuplicateRotateApply(const ManipulatorContext& ctx, Vec3d axis, Vec3d pivot)
{
    std::vector<ObjectTarget> items = GatherObjectTargets(ctx);
    if (items.empty())
        return nullptr;
    return std::make_unique<DuplicateRotateApply>(std::move(items), axis, pivot, ctx.Sink);
}

std::unique_ptr<IRotateApply> MakeExtrudeRotateApply(const ManipulatorContext& ctx, MeshElementKind kind,
                                                     Vec3d axis, Vec3d pivot)
{
    if (kind != MeshElementKind::Face)
        return nullptr; // an edge has no extrude normal; edges fall back to a plain rotate

    std::optional<ElementTarget> r = ResolveElementTarget(ctx, kind);
    if (!r.has_value() || r->Elements.empty())
        return nullptr;

    // Extrude one step along the average world normal of the selected faces, so
    // the cap has clearance to swing.
    Vec3d normalSum{ 0.0f, 0.0f, 0.0f };
    for (const SelectableRef& ref : r->Elements)
        if (const auto face = MeshElements::TryGetFace(r->Mesh, r->Transform, ref.ElementId))
            normalSum = normalSum + face->Normal;
    if (normalSum.SqrMagnitude() <= 0.0f)
        return nullptr;
    const float step = ctx.Grid.SnapEnabled ? ctx.Grid.Spacing : ctx.Grid.Spacing * 0.25f;
    const Vec3d offset = normalSum.Normalized() * step;

    std::optional<MeshEditService::ExtrudeResult> extruded =
        ctx.Service.ExtrudeElements(r->Mesh, r->Transform, r->Elements, kind, offset, true);
    if (!extruded.has_value() || extruded->NewElementIds.empty())
        return nullptr;

    const RegistryId registry = r->Elements.front().Registry;
    return std::make_unique<ExtrudeRotateApply>(r->Entity, std::move(r->Mesh), std::move(*extruded),
                                                registry, r->Transform, kind, axis, pivot,
                                                ctx.Service, ctx.Sink);
}

std::unique_ptr<IRotateApply> MakeElementRotateApply(const ManipulatorContext& ctx, MeshElementKind kind,
                                                     Vec3d axis, Vec3d pivot)
{
    std::optional<ElementTarget> r = ResolveElementTarget(ctx, kind);
    if (!r.has_value())
        return nullptr;
    return std::make_unique<ElementRotateApply>(
        r->Entity, std::move(r->Mesh), r->Transform, std::move(r->Elements), kind, axis, pivot, ctx.Service, ctx.Sink);
}

class RotateDrag : public IInteraction
{
public:
    RotateDrag(Vec3d pivot, Vec3d axis, Vec3d u, Vec3d v, double startAngle, std::unique_ptr<IRotateApply> apply)
        : Pivot(pivot), Axis(axis), U(u), V(v), PrevAngle(startAngle), Apply(std::move(apply)) {}

    void OnPointerMove(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override
    {
        if (!UpdateAngle(viewport, pointer.Position))
            return;
        const double radians = Snapped(ctx, viewport);
        Apply->Preview(radians);
        WriteReadout(ctx, viewport, radians);
    }

    void OnPointerUp(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override
    {
        UpdateAngle(viewport, pointer.Position);
        Apply->Commit(Snapped(ctx, viewport));
        ctx.Overlay.Readout.Clear();
    }

    void OnCancel(ToolContext& ctx) override
    {
        Apply->Cancel();
        ctx.Overlay.Readout.Clear();
    }

private:
    bool UpdateAngle(const EditorViewport& viewport, ImVec2 pos)
    {
        const Ray3d ray = ViewportProjection(viewport).RayThroughPixel(pos);
        const std::optional<Vec3d> hit = RayPlaneHit(ray, Pivot, Axis);
        if (!hit.has_value())
            return false;
        const Vec3d rel = *hit - Pivot;
        const double angle = std::atan2(rel.Dot(V), rel.Dot(U));
        // Accumulate unwrapped so a drag past +/-180 keeps turning instead of flipping.
        double delta = angle - PrevAngle;
        if (delta > kPi) delta -= 2.0 * kPi;
        if (delta < -kPi) delta += 2.0 * kPi;
        Accumulated += delta;
        PrevAngle = angle;
        return true;
    }

    double Snapped(ToolContext& ctx, const EditorViewport& viewport) const
    {
        if (!viewport.GetGrid(ctx.Grid).SnapEnabled)
            return Accumulated;
        const double step = kSnapDegrees * kPi / 180.0;
        return std::round(Accumulated / step) * step;
    }

    void WriteReadout(ToolContext& ctx, const EditorViewport& viewport, double radians)
    {
        const float radius = RingRadius(viewport, Pivot);
        const Vec3d dir = U * static_cast<float>(std::cos(PrevAngle)) + V * static_cast<float>(std::sin(PrevAngle));
        DragReadout& readout = ctx.Overlay.Readout;
        readout.From = Pivot;
        readout.To = Pivot + dir * radius;
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.0f°", radians * 180.0 / kPi);
        readout.Text = buffer;
        readout.Viewport = viewport.Id;
    }

    Vec3d Pivot;
    Vec3d Axis;
    Vec3d U;
    Vec3d V;
    double PrevAngle;
    double Accumulated = 0.0;
    std::unique_ptr<IRotateApply> Apply;
};
}

bool RotateManipulator::AppliesTo(const ManipulatorContext& ctx, const EditorViewport&) const
{
    return GizmoPivot(ctx).has_value();
}

void RotateManipulator::BuildVisual(const ManipulatorContext& ctx,
                                    const EditorViewport& viewport,
                                    int hoveredPart,
                                    ManipulatorVisual& out) const
{
    const std::optional<Vec3d> pivot = GizmoPivot(ctx);
    if (!pivot.has_value())
        return;

    const float radius = RingRadius(viewport, *pivot);
    const std::array<int, 3> axes = { 1, 2, 3 };
    const std::array<Vec4, 3> colors = { EditorTheme::AxisX, EditorTheme::AxisY, EditorTheme::AxisZ };

    for (std::size_t i = 0; i < axes.size(); ++i)
    {
        Vec3d u;
        Vec3d v;
        RingBasis(ctx, axes[i], u, v);
        const Vec4 color = (axes[i] == hoveredPart) ? EditorTheme::Hover : colors[i];

        Vec3d prev = *pivot + u * radius;
        for (int s = 1; s <= kRingSegments; ++s)
        {
            const double t = 2.0 * kPi * static_cast<double>(s) / kRingSegments;
            const Vec3d point =
                *pivot + (u * static_cast<float>(std::cos(t)) + v * static_cast<float>(std::sin(t))) * radius;
            out.Lines.push_back({ .A = prev, .B = point, .Color = color });
            prev = point;
        }
    }
}

int RotateManipulator::HitTest(const ManipulatorContext& ctx,
                               const EditorViewport& viewport,
                               ImVec2 screenPos) const
{
    const std::optional<Vec3d> pivot = GizmoPivot(ctx);
    if (!pivot.has_value())
        return 0;

    const ViewportProjection projection(viewport);
    const float radius = RingRadius(viewport, *pivot);
    const std::array<int, 3> axes = { 1, 2, 3 };

    int best = 0;
    float bestPixels = kRingHitPixels;
    for (const int axis : axes)
    {
        Vec3d u;
        Vec3d v;
        RingBasis(ctx, axis, u, v);
        std::optional<ProjectedPoint> prev = projection.WorldToPixel(*pivot + u * radius);
        float ringMin = FLT_MAX;
        for (int s = 1; s <= kRingSegments; ++s)
        {
            const double t = 2.0 * kPi * static_cast<double>(s) / kRingSegments;
            const Vec3d world =
                *pivot + (u * static_cast<float>(std::cos(t)) + v * static_cast<float>(std::sin(t))) * radius;
            const std::optional<ProjectedPoint> cur = projection.WorldToPixel(world);
            if (prev.has_value() && cur.has_value())
                ringMin = std::min(ringMin,
                                   ViewportProjection::DistancePointToSegment(screenPos, prev->Pixel, cur->Pixel));
            prev = cur;
        }
        if (ringMin <= bestPixels)
        {
            bestPixels = ringMin;
            best = axis;
        }
    }
    return best;
}

std::unique_ptr<IInteraction> RotateManipulator::BeginDrag(
    int part,
    const ManipulatorContext& ctx,
    const EditorViewport& viewport,
    ImVec2 screenPos,
    ModifierFlags modifiers) const
{
    if (part < 1 || part > 3)
        return nullptr;

    const Vec3d axis = AxisDirection(ctx, part);
    const MeshElementKind kind = ctx.Service.GetElementKind();
    const std::optional<Vec3d> pivot = GizmoPivot(ctx);
    if (!pivot.has_value())
        return nullptr;

    Vec3d u;
    Vec3d v;
    RingBasis(ctx, part, u, v);
    double startAngle = 0.0;
    const Ray3d ray = ViewportProjection(viewport).RayThroughPixel(screenPos);
    if (const std::optional<Vec3d> hit = RayPlaneHit(ray, *pivot, axis))
    {
        const Vec3d rel = *hit - *pivot;
        startAngle = std::atan2(rel.Dot(v), rel.Dot(u));
    }

    // Grid-edit retargets the ring to the grid frame. Otherwise Shift turns the
    // drag into duplicate (object) or extrude-and-swing (face); anything that
    // cannot resolve falls back to the plain rotate, so Shift never dead-ends.
    std::unique_ptr<IRotateApply> apply;
    if (ctx.EditGridOrigin)
        apply = std::make_unique<GridFrameRotateApply>(ctx.Grid, axis);
    else if (modifiers.Shift && kind == MeshElementKind::Object)
        apply = MakeDuplicateRotateApply(ctx, axis, *pivot);
    else if (modifiers.Shift && kind != MeshElementKind::Object)
        apply = MakeExtrudeRotateApply(ctx, kind, axis, *pivot);
    if (apply == nullptr)
        apply = (kind == MeshElementKind::Object)
            ? MakeObjectRotateApply(ctx, axis, *pivot)
            : MakeElementRotateApply(ctx, kind, axis, *pivot);
    if (apply == nullptr)
        return nullptr;

    return std::make_unique<RotateDrag>(*pivot, axis, u, v, startAngle, std::move(apply));
}
