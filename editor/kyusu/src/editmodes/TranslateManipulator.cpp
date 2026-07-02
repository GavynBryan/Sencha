#include "TranslateManipulator.h"

#include "GizmoMath.h"
#include "ManipulatorTargets.h"
#include "SelectionPivot.h"
#include "EditorTheme.h"
#include "meshedit/ManipulationSink.h"
#include "meshedit/MeshEditService.h"
#include "meshedit/MeshElementKindTraits.h"
#include "overlay/EditorOverlayState.h"
#include "overlay/SelectionLabels.h"
#include "tools/ToolContext.h"
#include "viewport/EditorViewport.h"
#include "viewport/ViewportProjection.h"

#include <math/geometry/3d/Ray3d.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace
{
constexpr float kAxisHitPixels = 12.0f;

constexpr int kAxisX = 1;
constexpr int kAxisY = 2;
constexpr int kAxisZ = 3;

// Axis directions come from the session-resolved gizmo frame (world, grid, or
// local space); the part id indexes into it.
Vec3d AxisDirection(const ManipulatorContext& ctx, int axis)
{
    if (axis < kAxisX || axis > kAxisZ)
        return Vec3d(0.0f, 0.0f, 0.0f);
    return ctx.Axes[static_cast<std::size_t>(axis - kAxisX)];
}

// The point the gizmo sits on: the grid origin while grid-origin editing,
// otherwise the selection pivot.
std::optional<Vec3d> GizmoPivot(const ManipulatorContext& ctx)
{
    if (ctx.EditGridOrigin)
        return ctx.Grid.Origin;
    return ComputeSelectionPivot(ctx.Sink, ctx.Selection, ctx.Service.GetElementKind(), ctx.Pivot);
}

float AxisLength(const EditorViewport& viewport, Vec3d pivot)
{
    // Screen-constant: the gizmo is the same on-screen length at any zoom/distance,
    // in both ortho and perspective. (P4 visual pass.)
    return ViewportProjection(viewport).WorldSizeForPixels(pivot, EditorTheme::GizmoAxisPixels);
}

void PerpendicularBasis(Vec3d axis, Vec3d& outU, Vec3d& outV)
{
    const Vec3d reference = std::abs(axis.Y) < 0.99f ? Vec3d(0.0f, 1.0f, 0.0f)
                                                     : Vec3d(1.0f, 0.0f, 0.0f);
    outU = axis.Cross(reference).Normalized();
    outV = axis.Cross(outU).Normalized();
}

// Applies a translation delta to whatever is being manipulated, via the sink.
struct ITranslateApply
{
    virtual void Preview(Vec3d delta) = 0;
    virtual void Commit(Vec3d delta) = 0;
    virtual void Cancel() = 0;
    virtual ~ITranslateApply() = default;
};

// The move and the Shift-drag duplicate share the object/element gathering with
// rotate/scale (ManipulatorTargets); only the apply differs.
using ObjectItem = ObjectTarget;

[[nodiscard]] Transform3f WithDelta(const Transform3f& initial, Vec3d delta)
{
    Transform3f result = initial;
    result.Position += delta;
    return result;
}

class ObjectApply : public ITranslateApply
{
public:
    ObjectApply(std::vector<ObjectItem> items, ManipulationSink& sink)
        : Items(std::move(items)), Sink(sink) {}

    void Preview(Vec3d delta) override
    {
        for (const ObjectItem& item : Items)
            Sink.PreviewTransform(item.Entity, WithDelta(item.Initial, delta));
    }

    void Commit(Vec3d delta) override
    {
        std::vector<TransformEdit> edits;
        edits.reserve(Items.size());
        for (const ObjectItem& item : Items)
            edits.push_back({ item.Entity, item.Initial, WithDelta(item.Initial, delta) });
        Sink.CommitTransforms(edits);
    }

    void Cancel() override
    {
        for (const ObjectItem& item : Items)
            Sink.PreviewTransform(item.Entity, item.Initial);
    }

private:
    std::vector<ObjectItem> Items;
    ManipulationSink& Sink;
};

// Shift-drag in object mode: live copies are created on the first move and follow
// the cursor (the originals stay put), so the duplicate is visible during the
// drag. On release the preview copies are dropped and the same copies are
// committed as one undoable step (and selected); cancel just drops them.
class DuplicateApply : public ITranslateApply
{
public:
    DuplicateApply(std::vector<ObjectItem> items, ManipulationSink& sink)
        : Items(std::move(items)), Sink(sink) {}

    void Preview(Vec3d delta) override
    {
        if (Previews.empty())
        {
            std::vector<EntityId> sources;
            sources.reserve(Items.size());
            for (const ObjectItem& item : Items)
                sources.push_back(item.Entity);
            Previews = Sink.CreatePreviewDuplicates(sources);
        }
        for (std::size_t i = 0; i < Previews.size() && i < Items.size(); ++i)
            Sink.PreviewTransform(Previews[i], WithDelta(Items[i].Initial, delta));
    }

    void Commit(Vec3d delta) override
    {
        Sink.DestroyPreviewEntities(Previews);
        Previews.clear();

        // A Shift-click with no real drag should not stamp an in-place copy.
        if (delta.SqrMagnitude() <= 0.0f)
            return;

        std::vector<EntityId> sources;
        std::vector<Transform3f> transforms;
        sources.reserve(Items.size());
        transforms.reserve(Items.size());
        for (const ObjectItem& item : Items)
        {
            sources.push_back(item.Entity);
            transforms.push_back(WithDelta(item.Initial, delta));
        }
        Sink.CommitDuplicate(sources, transforms);
    }

    void Cancel() override
    {
        Sink.DestroyPreviewEntities(Previews);
        Previews.clear();
    }

private:
    std::vector<ObjectItem> Items;
    ManipulationSink& Sink;
    std::vector<EntityId> Previews;
};

class ElementApply : public ITranslateApply
{
public:
    ElementApply(EntityId entity, BrushMesh initial, Transform3f transform,
                 std::vector<SelectableRef> elements, MeshElementKind kind,
                 MeshEditService& service, ManipulationSink& sink)
        : Entity(entity), Initial(std::move(initial)), Transform(transform)
        , Elements(std::move(elements)), Kind(kind), Service(service), Sink(sink) {}

    void Preview(Vec3d delta) override
    {
        if (auto mesh = Service.TranslateElements(Initial, Transform, Elements, Kind, delta, false))
            Sink.PreviewMesh(Entity, *mesh);
    }

    void Commit(Vec3d delta) override
    {
        if (auto mesh = Service.TranslateElements(Initial, Transform, Elements, Kind, delta, true))
            Sink.CommitMesh(Entity, Initial, std::move(*mesh));
        else
            Sink.PreviewMesh(Entity, Initial); // unusable result: revert
    }

    void Cancel() override { Sink.PreviewMesh(Entity, Initial); }

private:
    EntityId Entity;
    BrushMesh Initial;
    Transform3f Transform;
    std::vector<SelectableRef> Elements;
    MeshElementKind Kind;
    MeshEditService& Service;
    ManipulationSink& Sink;
};

// Shift-drag on a face or edge: grow new geometry (cap + walls, or a new quad
// plane) offset by the axis-constrained drag, instead of moving the element.
class ExtrudeApply : public ITranslateApply
{
public:
    ExtrudeApply(EntityId entity, BrushMesh initial, Transform3f transform,
                 std::vector<SelectableRef> elements, MeshElementKind kind,
                 MeshEditService& service, ManipulationSink& sink)
        : Entity(entity), Initial(std::move(initial)), Transform(transform)
        , Elements(std::move(elements)), Kind(kind), Service(service), Sink(sink) {}

    void Preview(Vec3d delta) override
    {
        if (auto result = Service.ExtrudeElements(Initial, Transform, Elements, Kind, delta, true))
            Sink.PreviewMesh(Entity, result->Mesh);
        else
            Sink.PreviewMesh(Entity, Initial); // below the extrude threshold: show original
    }

    void Commit(Vec3d delta) override
    {
        auto result = Service.ExtrudeElements(Initial, Transform, Elements, Kind, delta, true);
        if (!result.has_value())
        {
            Sink.PreviewMesh(Entity, Initial); // no real extrude: revert, commit nothing
            return;
        }

        Sink.CommitMesh(Entity, Initial, std::move(result->Mesh));
        // The mesh reindexed; select the freshly created outer cap/edge so it can be
        // extruded or moved again (empty refs clears, as MeshEditPanel does).
        const RegistryId registry = Elements.empty() ? RegistryId::Invalid() : Elements.front().Registry;
        std::vector<SelectableRef> refs;
        refs.reserve(result->NewElementIds.size());
        for (std::uint32_t id : result->NewElementIds)
            refs.push_back(Kind == MeshElementKind::Face
                               ? SelectableRef::FaceSelection(registry, Entity, id)
                               : SelectableRef::EdgeSelection(registry, Entity, id));
        Sink.SelectElements(refs);
    }

    void Cancel() override { Sink.PreviewMesh(Entity, Initial); }

private:
    EntityId Entity;
    BrushMesh Initial;
    Transform3f Transform;
    std::vector<SelectableRef> Elements;
    MeshElementKind Kind;
    MeshEditService& Service;
    ManipulationSink& Sink;
};

// Pivot edit: the axis drag moves the transient pivot (Override) instead of the
// selection. The pivot persists after release and resets when the selection
// changes; cancel restores the prior override.
class PivotApply : public ITranslateApply
{
public:
    PivotApply(PivotState& pivot, Vec3d start)
        : Pivot(pivot), Start(start), Previous(pivot.Override) {}

    void Preview(Vec3d delta) override { Pivot.Override = Start + delta; }
    void Commit(Vec3d delta) override { Pivot.Override = Start + delta; }
    void Cancel() override { Pivot.Override = Previous; }

private:
    PivotState& Pivot;
    Vec3d Start;
    std::optional<Vec3d> Previous;
};

// Grid-origin edit: the axis drag moves the workspace grid origin (view state,
// so no undo step); cancel restores the prior origin.
class GridOriginApply : public ITranslateApply
{
public:
    explicit GridOriginApply(GridSettings& grid)
        : Grid(grid), Start(grid.Origin) {}

    void Preview(Vec3d delta) override { Grid.Origin = Start + delta; }
    void Commit(Vec3d delta) override { Grid.Origin = Start + delta; }
    void Cancel() override { Grid.Origin = Start; }

private:
    GridSettings& Grid;
    Vec3d Start;
};

class TranslateDrag : public IInteraction
{
public:
    TranslateDrag(Vec3d pivot, Vec3d axisDir, double startParam,
                  std::unique_ptr<ITranslateApply> apply)
        : Pivot(pivot), AxisDir(axisDir), StartParam(startParam), Apply(std::move(apply)) {}

    void OnPointerMove(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override
    {
        if (UpdateDelta(ctx, viewport, pointer.Position))
        {
            Apply->Preview(LastDelta);
            // Blue origin->current line + distance moved, shown until release.
            DragReadout& readout = ctx.Overlay.Readout;
            readout.From = Pivot;
            readout.To = Pivot + LastDelta;
            readout.Text = FormatUnits(LastDelta.Magnitude());
            readout.Viewport = viewport.Id;
        }
    }

    void OnPointerUp(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override
    {
        UpdateDelta(ctx, viewport, pointer.Position);
        Apply->Commit(LastDelta);
        ctx.Overlay.Readout.Clear();
    }

    void OnCancel(ToolContext& ctx) override
    {
        Apply->Cancel();
        ctx.Overlay.Readout.Clear();
    }

private:
    bool UpdateDelta(ToolContext& ctx, const EditorViewport& viewport, ImVec2 pos)
    {
        const std::optional<double> s =
            GizmoMath::ClosestAxisParam(Pivot, AxisDir, ViewportProjection(viewport).RayThroughPixel(pos));
        if (!s.has_value())
            return false;
        const GridPlane grid = viewport.GetGrid(ctx.Grid);
        const double rawOffset = *s - StartParam;
        // Honor the grid-snap toggle: snap the moved coordinate to grid lines, or
        // move freely when snapping is off.
        const double offset = grid.SnapEnabled
            ? GizmoMath::SnapAxisOffset(rawOffset, Pivot.Dot(AxisDir), grid.Origin.Dot(AxisDir), grid.Spacing)
            : rawOffset;
        LastDelta = AxisDir * static_cast<float>(offset);
        return true;
    }

    Vec3d Pivot;
    Vec3d AxisDir;
    double StartParam;
    Vec3d LastDelta = {};
    std::unique_ptr<ITranslateApply> Apply;
};

std::unique_ptr<ITranslateApply> MakeObjectApply(const ManipulatorContext& ctx)
{
    // Move every selected entity — one drag, one undo, all of them.
    std::vector<ObjectItem> items = GatherObjectTargets(ctx);
    if (items.empty())
        return nullptr;
    return std::make_unique<ObjectApply>(std::move(items), ctx.Sink);
}

std::unique_ptr<ITranslateApply> MakeDuplicateApply(const ManipulatorContext& ctx)
{
    std::vector<ObjectItem> items = GatherObjectTargets(ctx);
    if (items.empty())
        return nullptr;
    return std::make_unique<DuplicateApply>(std::move(items), ctx.Sink);
}

std::unique_ptr<ITranslateApply> MakeElementApply(const ManipulatorContext& ctx, MeshElementKind kind)
{
    std::optional<ElementTarget> r = ResolveElementTarget(ctx, kind);
    if (!r.has_value())
        return nullptr;
    return std::make_unique<ElementApply>(
        r->Entity, std::move(r->Mesh), r->Transform, std::move(r->Elements), kind, ctx.Service, ctx.Sink);
}

std::unique_ptr<ITranslateApply> MakeExtrudeApply(const ManipulatorContext& ctx, MeshElementKind kind)
{
    std::optional<ElementTarget> r = ResolveElementTarget(ctx, kind);
    if (!r.has_value())
        return nullptr;
    return std::make_unique<ExtrudeApply>(
        r->Entity, std::move(r->Mesh), r->Transform, std::move(r->Elements), kind, ctx.Service, ctx.Sink);
}
}

bool TranslateManipulator::AppliesTo(const ManipulatorContext& ctx, const EditorViewport&) const
{
    return GizmoPivot(ctx).has_value();
}

void TranslateManipulator::BuildVisual(const ManipulatorContext& ctx,
                                       const EditorViewport& viewport,
                                       int hoveredPart,
                                       ManipulatorVisual& out) const
{
    const std::optional<Vec3d> pivot = GizmoPivot(ctx);
    if (!pivot.has_value())
        return;

    const float length = AxisLength(viewport, *pivot);
    const float headLength = length * 0.18f;
    const float headRadius = headLength * 0.45f;

    const std::array<int, 3> axes = { kAxisX, kAxisY, kAxisZ };
    const std::array<Vec4, 3> colors = { EditorTheme::AxisX, EditorTheme::AxisY, EditorTheme::AxisZ };

    for (std::size_t i = 0; i < axes.size(); ++i)
    {
        const Vec3d dir = AxisDirection(ctx, axes[i]);
        const Vec4 color = (axes[i] == hoveredPart) ? EditorTheme::Hover : colors[i];
        const Vec3d tip = *pivot + dir * length;
        const Vec3d base = tip - dir * headLength;
        out.Lines.push_back({ .A = *pivot, .B = tip, .Color = color });

        Vec3d u;
        Vec3d v;
        PerpendicularBasis(dir, u, v);
        const std::array<Vec3d, 4> ring = {
            base + u * headRadius, base + v * headRadius, base - u * headRadius, base - v * headRadius,
        };
        for (std::size_t c = 0; c < ring.size(); ++c)
        {
            out.Lines.push_back({ .A = tip, .B = ring[c], .Color = color });
            out.Lines.push_back({ .A = ring[c], .B = ring[(c + 1) % ring.size()], .Color = color });
        }
    }
}

int TranslateManipulator::HitTest(const ManipulatorContext& ctx,
                                  const EditorViewport& viewport,
                                  ImVec2 screenPos) const
{
    const std::optional<Vec3d> pivot = GizmoPivot(ctx);
    if (!pivot.has_value())
        return 0;

    const ViewportProjection projection(viewport);
    const std::optional<ProjectedPoint> origin = projection.WorldToPixel(*pivot);
    if (!origin.has_value())
        return 0;

    const float length = AxisLength(viewport, *pivot);

    int best = 0;
    float bestPixels = kAxisHitPixels;
    for (const int axis : { kAxisX, kAxisY, kAxisZ })
    {
        const std::optional<ProjectedPoint> end =
            projection.WorldToPixel(*pivot + AxisDirection(ctx, axis) * length);
        if (!end.has_value())
            continue;
        const float pixels =
            ViewportProjection::DistancePointToSegment(screenPos, origin->Pixel, end->Pixel);
        if (pixels <= bestPixels)
        {
            bestPixels = pixels;
            best = axis;
        }
    }
    return best;
}

std::unique_ptr<IInteraction> TranslateManipulator::BeginDrag(
    int part,
    const ManipulatorContext& ctx,
    const EditorViewport& viewport,
    ImVec2 screenPos,
    ModifierFlags modifiers) const
{
    const Vec3d axisDir = AxisDirection(ctx, part);
    if (part == 0 || axisDir.SqrMagnitude() == 0.0f)
        return nullptr;

    const MeshElementKind kind = ctx.Service.GetElementKind();
    const std::optional<Vec3d> pivot = GizmoPivot(ctx);
    if (!pivot.has_value())
        return nullptr;

    const std::optional<double> startParam =
        GizmoMath::ClosestAxisParam(*pivot, axisDir, ViewportProjection(viewport).RayThroughPixel(screenPos));
    if (!startParam.has_value())
        return nullptr;

    // Grid-origin and pivot edit retarget the drag off the selection. Otherwise
    // Shift turns the drag into duplicate (object) or extrude (face/edge); vertex
    // mode and the plain drag stay a move. Extrude falls back to a move if no
    // face/edge refs resolve, so Shift never dead-ends the drag.
    std::unique_ptr<ITranslateApply> apply;
    if (ctx.EditGridOrigin)
        apply = std::make_unique<GridOriginApply>(ctx.Grid);
    else if (ctx.Pivot.Editing)
        apply = std::make_unique<PivotApply>(ctx.Pivot, *pivot);
    else if (kind == MeshElementKind::Object)
        apply = modifiers.Shift ? MakeDuplicateApply(ctx) : MakeObjectApply(ctx);
    else if (modifiers.Shift && (kind == MeshElementKind::Face || kind == MeshElementKind::Edge))
        apply = MakeExtrudeApply(ctx, kind);
    if (apply == nullptr)
        apply = (kind == MeshElementKind::Object) ? MakeObjectApply(ctx) : MakeElementApply(ctx, kind);
    if (apply == nullptr)
        return nullptr;

    return std::make_unique<TranslateDrag>(*pivot, axisDir, *startParam, std::move(apply));
}
