#include "ScaleManipulator.h"

#include "GizmoMath.h"
#include "ManipulatorTargets.h"
#include "SelectionPivot.h"
#include "EditorTheme.h"
#include "meshedit/ElementGeometry.h"
#include "meshedit/ManipulationSink.h"
#include "meshedit/MeshEditService.h"
#include "meshedit/MeshElements.h"
#include "overlay/EditorOverlayState.h"
#include "tools/ToolContext.h"
#include "viewport/EditorViewport.h"
#include "viewport/ViewportProjection.h"

#include <math/geometry/3d/Ray3d.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace
{
constexpr float kStalkPixels = 80.0f;   // screen-constant axis length
constexpr float kBoxPixels = 6.0f;      // end-box half size
constexpr float kHitPixels = 10.0f;     // cursor-to-handle tolerance
// The uniform-scale center box: larger than the axis end boxes, both drawn
// and hit, so the highest-traffic handle is not the hardest one to grab.
constexpr float kCenterBoxPixels = 10.0f;
constexpr float kCenterHitPixels = 14.0f;
constexpr float kMinFactor = 0.01f;     // never invert/collapse the geometry
constexpr int kUniformPart = 4;

Vec3d AxisDirection(int part)
{
    switch (part)
    {
    case 1: return Vec3d(1.0f, 0.0f, 0.0f);
    case 2: return Vec3d(0.0f, 1.0f, 0.0f);
    case 3: return Vec3d(0.0f, 0.0f, 1.0f);
    default: return Vec3d(0.0f, 0.0f, 0.0f);
    }
}

float StalkLength(const EditorViewport& viewport, Vec3d pivot)
{
    return ViewportProjection(viewport).WorldSizeForPixels(pivot, kStalkPixels);
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

struct IScaleApply
{
    virtual void Preview(Vec3d factor) = 0;
    virtual void Commit(Vec3d factor) = 0;
    virtual void Cancel() = 0;
    virtual ~IScaleApply() = default;
};

Transform3f ScaledTransform(const Transform3f& initial, Vec3d factor, Vec3d pivot)
{
    Transform3f result = initial;
    const Vec3d rel = initial.Position - pivot;
    result.Position = pivot + Vec3d(rel.X * factor.X, rel.Y * factor.Y, rel.Z * factor.Z);
    result.Scale = Vec3d(initial.Scale.X * factor.X, initial.Scale.Y * factor.Y, initial.Scale.Z * factor.Z);
    return result;
}

class ObjectScaleApply : public IScaleApply
{
public:
    ObjectScaleApply(std::vector<ObjectTarget> items, Vec3d pivot, ManipulationSink& sink)
        : Items(std::move(items)), Pivot(pivot), Sink(sink) {}

    void Preview(Vec3d factor) override
    {
        for (const ObjectTarget& item : Items)
            Sink.PreviewTransform(item.Entity, ScaledTransform(item.Initial, factor, Pivot));
    }

    void Commit(Vec3d factor) override
    {
        std::vector<TransformEdit> edits;
        edits.reserve(Items.size());
        for (const ObjectTarget& item : Items)
            edits.push_back({ item.Entity, item.Initial, ScaledTransform(item.Initial, factor, Pivot) });
        Sink.CommitTransforms(edits);
    }

    void Cancel() override
    {
        for (const ObjectTarget& item : Items)
            Sink.PreviewTransform(item.Entity, item.Initial);
    }

private:
    std::vector<ObjectTarget> Items;
    Vec3d Pivot;
    ManipulationSink& Sink;
};

class ElementScaleApply : public IScaleApply
{
public:
    ElementScaleApply(std::vector<ElementTarget> targets, MeshElementKind kind,
                      Vec3d pivot, MeshEditService& service, ManipulationSink& sink)
        : Targets(std::move(targets)), Kind(kind), Pivot(pivot), Service(service), Sink(sink) {}

    void Preview(Vec3d factor) override
    {
        for (const ElementTarget& t : Targets)
            if (auto mesh = Service.ScaleElements(t.Mesh, t.Transform, t.Elements, Kind, factor, Pivot, false))
                Sink.PreviewMesh(t.Entity, *mesh);
    }

    void Commit(Vec3d factor) override
    {
        // Per-target: an unusable result reverts alone; the rest land as one
        // undo step.
        std::vector<MeshEdit> edits;
        edits.reserve(Targets.size());
        for (const ElementTarget& t : Targets)
        {
            if (auto mesh = Service.ScaleElements(t.Mesh, t.Transform, t.Elements, Kind, factor, Pivot, true))
                edits.push_back({ t.Entity, t.Mesh, std::move(*mesh) });
            else
                Sink.PreviewMesh(t.Entity, t.Mesh);
        }
        Sink.CommitMeshes(std::move(edits));
    }

    void Cancel() override
    {
        for (const ElementTarget& t : Targets)
            Sink.PreviewMesh(t.Entity, t.Mesh);
    }

private:
    std::vector<ElementTarget> Targets;
    MeshElementKind Kind;
    Vec3d Pivot;
    MeshEditService& Service;
    ManipulationSink& Sink;
};

// Shift-drag in face mode: extrude the selection a step along its average
// normal, then scale the freshly created cap by the drag factor (a tapered
// extrusion in one gesture). The extrude happens once up front; commit selects
// the new cap so the gesture chains, cancel restores the original mesh.
class ExtrudeScaleApply : public IScaleApply
{
public:
    ExtrudeScaleApply(EntityId entity, BrushMesh initial,
                      MeshEditService::ExtrudeResult extruded, RegistryId registry,
                      Transform3f transform, Vec3d pivot,
                      MeshEditService& service, ManipulationSink& sink)
        : Entity(entity), Initial(std::move(initial)), Extruded(std::move(extruded))
        , Transform(transform), Pivot(pivot), Service(service), Sink(sink)
    {
        NewRefs.reserve(Extruded.NewElementIds.size());
        for (std::uint32_t id : Extruded.NewElementIds)
            NewRefs.push_back(SelectableRef::FaceSelection(registry, entity, id));
    }

    void Preview(Vec3d factor) override
    {
        if (auto mesh = Service.ScaleElements(Extruded.Mesh, Transform, NewRefs, MeshElementKind::Face, factor, Pivot, false))
            Sink.PreviewMesh(Entity, *mesh);
    }

    void Commit(Vec3d factor) override
    {
        auto mesh = Service.ScaleElements(Extruded.Mesh, Transform, NewRefs, MeshElementKind::Face, factor, Pivot, true);
        if (!mesh.has_value())
        {
            Sink.PreviewMesh(Entity, Initial); // unusable result: revert, commit nothing
            return;
        }
        Sink.CommitMesh(Entity, Initial, std::move(*mesh));
        Sink.SelectElements(NewRefs);
    }

    void Cancel() override { Sink.PreviewMesh(Entity, Initial); }

private:
    EntityId Entity;
    BrushMesh Initial;
    MeshEditService::ExtrudeResult Extruded;
    Transform3f Transform;
    Vec3d Pivot;
    MeshEditService& Service;
    ManipulationSink& Sink;
    std::vector<SelectableRef> NewRefs;
};

std::unique_ptr<IScaleApply> MakeObjectScaleApply(const ManipulatorContext& ctx, Vec3d pivot)
{
    std::vector<ObjectTarget> items = GatherObjectTargets(ctx);
    if (items.empty())
        return nullptr;
    return std::make_unique<ObjectScaleApply>(std::move(items), pivot, ctx.Sink);
}

std::unique_ptr<IScaleApply> MakeElementScaleApply(const ManipulatorContext& ctx, MeshElementKind kind, Vec3d pivot)
{
    std::vector<ElementTarget> targets = ResolveElementTargets(ctx, kind);
    if (targets.empty())
        return nullptr;
    return std::make_unique<ElementScaleApply>(std::move(targets), kind, pivot, ctx.Service, ctx.Sink);
}

std::unique_ptr<IScaleApply> MakeExtrudeScaleApply(const ManipulatorContext& ctx, Vec3d pivot)
{
    // Extrude stays single-mesh: it acts on the primary's mesh (the front target).
    std::vector<ElementTarget> targets = ResolveElementTargets(ctx, MeshElementKind::Face);
    if (targets.empty() || targets.front().Elements.empty())
        return nullptr;
    ElementTarget* r = &targets.front();

    // Extrude one step along the average world normal of the selected faces so
    // the cap separates from its source plane before the scale shapes it.
    Vec3d normalSum{ 0.0f, 0.0f, 0.0f };
    for (const SelectableRef& ref : r->Elements)
        if (const auto face = MeshElements::TryGetFace(r->Mesh, r->Transform, ref.ElementId))
            normalSum = normalSum + face->Normal;
    if (normalSum.SqrMagnitude() <= 0.0f)
        return nullptr;
    const float step = ctx.Grid.SnapEnabled ? ctx.Grid.Spacing : ctx.Grid.Spacing * 0.25f;
    const Vec3d offset = normalSum.Normalized() * step;

    std::optional<MeshEditService::ExtrudeResult> extruded =
        ctx.Service.ExtrudeElements(r->Mesh, r->Transform, r->Elements, MeshElementKind::Face, offset, true);
    if (!extruded.has_value() || extruded->NewElementIds.empty())
        return nullptr;

    // Scale about the cap's own plane (pivot moved by the extrude offset) so a
    // uniform scale shapes the cap in place instead of dragging it back toward
    // the source face.
    const RegistryId registry = r->Elements.front().Registry;
    return std::make_unique<ExtrudeScaleApply>(r->Entity, std::move(r->Mesh), std::move(*extruded), registry,
                                               r->Transform, pivot + offset, ctx.Service, ctx.Sink);
}

// World-space coordinate along `axisDir` of the selection AABB bound that grid
// snapping drives: the side farther from the pivot, which is the side an axis
// scale moves the most. nullopt when nothing resolves.
std::optional<double> SelectionBoundCoord(const ManipulatorContext& ctx, MeshElementKind kind,
                                          Vec3d axisDir, Vec3d pivot)
{
    double minCoord = std::numeric_limits<double>::max();
    double maxCoord = std::numeric_limits<double>::lowest();
    bool any = false;
    const auto accumulate = [&](Vec3d world) {
        const double c = world.Dot(axisDir);
        minCoord = std::min(minCoord, c);
        maxCoord = std::max(maxCoord, c);
        any = true;
    };

    if (kind == MeshElementKind::Object)
    {
        for (const SelectableRef& ref : ctx.Selection.Items)
        {
            if (!ref.IsValid() || !ref.IsEntity())
                continue;
            const std::optional<MeshEditTargetMesh> resolved = ctx.Sink.ResolveMesh(ref.Entity);
            if (!resolved.has_value() || resolved->Mesh == nullptr)
                continue;
            for (const auto& vertex : resolved->Mesh->Vertices)
                accumulate(resolved->Transform.TransformPoint(vertex.Position));
        }
    }
    else
    {
        for (const ElementTarget& t : ResolveElementTargets(ctx, kind))
            for (const SelectableRef& ref : t.Elements)
                for (std::uint32_t index : ElementVertexIndices(t.Mesh, t.Transform, ref))
                    accumulate(t.Transform.TransformPoint(t.Mesh.Vertices[index].Position));
    }

    if (!any)
        return std::nullopt;
    const double p = pivot.Dot(axisDir);
    return (maxCoord - p >= p - minCoord) ? maxCoord : minCoord;
}

float ScreenDistance(ImVec2 a, ImVec2 b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

class ScaleDrag : public IInteraction
{
public:
    ScaleDrag(Vec3d pivot, int part, Vec3d axisDir, double startParam,
              ImVec2 pivotScreen, float startScreenDist, std::optional<double> boundOffset,
              std::unique_ptr<IScaleApply> apply)
        : Pivot(pivot), Part(part), AxisDir(axisDir), StartParam(startParam)
        , PivotScreen(pivotScreen), StartScreenDist(startScreenDist)
        , BoundOffset(boundOffset), Apply(std::move(apply)) {}

    void OnPointerMove(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override
    {
        const Vec3d factor = FactorAt(ctx, viewport, pointer.Position);
        Apply->Preview(factor);
        WriteReadout(ctx, viewport, factor);
    }

    void OnPointerUp(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override
    {
        Apply->Commit(FactorAt(ctx, viewport, pointer.Position));
        ctx.Overlay.Readout.Clear();
    }

    void OnCancel(ToolContext& ctx) override
    {
        Apply->Cancel();
        ctx.Overlay.Readout.Clear();
    }

private:
    Vec3d FactorAt(ToolContext& ctx, const EditorViewport& viewport, ImVec2 pos) const
    {
        if (Part >= 1 && Part <= 3)
        {
            const std::optional<double> s =
                GizmoMath::ClosestAxisParam(Pivot, AxisDir, ViewportProjection(viewport).RayThroughPixel(pos));
            if (!s.has_value() || std::abs(StartParam) < 1.0e-6)
                return Vec3d(1.0f, 1.0f, 1.0f);
            double f = std::max(static_cast<double>(kMinFactor), *s / StartParam);
            // Honor the grid-snap toggle: land the selection's AABB face on a
            // grid line rather than snapping the factor to arbitrary steps.
            const GridPlane grid = viewport.GetGrid(ctx.Grid);
            if (grid.SnapEnabled && BoundOffset.has_value())
            {
                const double pivotCoord = Pivot.Dot(AxisDir);
                f = std::max(static_cast<double>(kMinFactor),
                             GizmoMath::SnapScaleFactor(f, pivotCoord, pivotCoord + *BoundOffset,
                                                        grid.Origin.Dot(AxisDir), grid.Spacing));
            }
            Vec3d factor(1.0f, 1.0f, 1.0f);
            factor[Part - 1] = static_cast<float>(f);
            return factor;
        }

        // Uniform: screen-space distance ratio from the pivot.
        if (StartScreenDist < 1.0e-3f)
            return Vec3d(1.0f, 1.0f, 1.0f);
        const float f = std::max(kMinFactor, ScreenDistance(pos, PivotScreen) / StartScreenDist);
        return Vec3d(f, f, f);
    }

    void WriteReadout(ToolContext& ctx, const EditorViewport& viewport, Vec3d factor)
    {
        const float shown = (Part >= 1 && Part <= 3) ? factor[Part - 1] : factor.X;
        DragReadout& readout = ctx.Overlay.Readout;
        readout.From = Pivot;
        if (Part >= 1 && Part <= 3)
            readout.To = Pivot + AxisDir * (StalkLength(viewport, Pivot) * shown);
        else
            readout.To = Pivot;
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "x%.2f", static_cast<double>(shown));
        readout.Text = buffer;
        readout.Viewport = viewport.Id;
    }

    Vec3d Pivot;
    int Part;
    Vec3d AxisDir;
    double StartParam;
    ImVec2 PivotScreen;
    float StartScreenDist;
    // AABB bound minus pivot along AxisDir at drag start, for absolute grid snap.
    std::optional<double> BoundOffset;
    std::unique_ptr<IScaleApply> Apply;
};
}

bool ScaleManipulator::AppliesTo(const ManipulatorContext& ctx, const EditorViewport&) const
{
    return ComputeSelectionPivot(ctx.Sink, ctx.Selection, ctx.Service.GetElementKind(), ctx.Pivot).has_value();
}

void ScaleManipulator::BuildVisual(const ManipulatorContext& ctx,
                                   const EditorViewport& viewport,
                                   int hoveredPart,
                                   ManipulatorVisual& out) const
{
    const std::optional<Vec3d> pivot =
        ComputeSelectionPivot(ctx.Sink, ctx.Selection, ctx.Service.GetElementKind(), ctx.Pivot);
    if (!pivot.has_value())
        return;

    const ViewportProjection projection(viewport);
    const float length = StalkLength(viewport, *pivot);
    const std::array<int, 3> axes = { 1, 2, 3 };
    const std::array<Vec4, 3> colors = { EditorTheme::AxisX, EditorTheme::AxisY, EditorTheme::AxisZ };

    for (std::size_t i = 0; i < axes.size(); ++i)
    {
        const Vec3d dir = AxisDirection(axes[i]);
        const Vec3d tip = *pivot + dir * length;
        const Vec4 color = (axes[i] == hoveredPart) ? EditorTheme::Hover : colors[i];
        out.Lines.push_back({ .A = *pivot, .B = tip, .Color = color });
        AppendBox(out, tip, projection.WorldSizeForPixels(tip, kBoxPixels), color);
    }

    // Center box for uniform scale.
    const Vec4 centerColor = (hoveredPart == kUniformPart) ? EditorTheme::Hover : EditorTheme::Handle;
    AppendBox(out, *pivot, projection.WorldSizeForPixels(*pivot, kCenterBoxPixels), centerColor);
}

int ScaleManipulator::HitTest(const ManipulatorContext& ctx,
                              const EditorViewport& viewport,
                              ImVec2 screenPos) const
{
    const std::optional<Vec3d> pivot =
        ComputeSelectionPivot(ctx.Sink, ctx.Selection, ctx.Service.GetElementKind(), ctx.Pivot);
    if (!pivot.has_value())
        return 0;

    const ViewportProjection projection(viewport);
    const float length = StalkLength(viewport, *pivot);

    int best = 0;
    float bestPixels = kHitPixels;

    // Axis end boxes (and their stalks).
    for (const int axis : { 1, 2, 3 })
    {
        const std::optional<ProjectedPoint> origin = projection.WorldToPixel(*pivot);
        const std::optional<ProjectedPoint> tip = projection.WorldToPixel(*pivot + AxisDirection(axis) * length);
        if (!origin.has_value() || !tip.has_value())
            continue;
        const float toTip = ScreenDistance(screenPos, tip->Pixel);
        const float toStalk = ViewportProjection::DistancePointToSegment(screenPos, origin->Pixel, tip->Pixel);
        const float pixels = std::min(toTip, toStalk);
        if (pixels <= bestPixels)
        {
            bestPixels = pixels;
            best = axis;
        }
    }

    // Center box (uniform) wins ties against a stalk passing through the pivot.
    if (const std::optional<ProjectedPoint> center = projection.WorldToPixel(*pivot))
    {
        const float toCenter = ScreenDistance(screenPos, center->Pixel);
        if (toCenter <= kCenterHitPixels && (best == 0 || toCenter <= bestPixels))
        {
            bestPixels = toCenter;
            best = kUniformPart;
        }
    }
    return best;
}

std::unique_ptr<IInteraction> ScaleManipulator::BeginDrag(
    int part,
    const ManipulatorContext& ctx,
    const EditorViewport& viewport,
    ImVec2 screenPos,
    ModifierFlags modifiers) const
{
    if (part < 1 || part > kUniformPart)
        return nullptr;

    const MeshElementKind kind = ctx.Service.GetElementKind();
    const std::optional<Vec3d> pivot = ComputeSelectionPivot(ctx.Sink, ctx.Selection, kind, ctx.Pivot);
    if (!pivot.has_value())
        return nullptr;

    const ViewportProjection projection(viewport);
    const Vec3d axisDir = AxisDirection(part);
    double startParam = 1.0;
    if (part >= 1 && part <= 3)
    {
        const std::optional<double> s =
            GizmoMath::ClosestAxisParam(*pivot, axisDir, projection.RayThroughPixel(screenPos));
        if (!s.has_value())
            return nullptr;
        startParam = *s;
    }

    ImVec2 pivotScreen = screenPos;
    if (const std::optional<ProjectedPoint> p = projection.WorldToPixel(*pivot))
        pivotScreen = p->Pixel;
    const float startScreenDist = ScreenDistance(screenPos, pivotScreen);

    // The bound is captured before the extrude variant grows geometry, so the
    // snap drives the pre-drag selection's AABB face.
    std::optional<double> boundOffset;
    if (part >= 1 && part <= 3)
        if (const std::optional<double> bound = SelectionBoundCoord(ctx, kind, axisDir, *pivot))
            boundOffset = *bound - pivot->Dot(axisDir);

    // Shift in face mode turns the drag into extrude-then-scale (a tapered
    // step); it falls back to a plain scale if no faces resolve, so Shift never
    // dead-ends the drag.
    std::unique_ptr<IScaleApply> apply;
    if (kind == MeshElementKind::Object)
        apply = MakeObjectScaleApply(ctx, *pivot);
    else if (modifiers.Shift && kind == MeshElementKind::Face)
        apply = MakeExtrudeScaleApply(ctx, *pivot);
    if (apply == nullptr && kind != MeshElementKind::Object)
        apply = MakeElementScaleApply(ctx, kind, *pivot);
    if (apply == nullptr)
        return nullptr;

    return std::make_unique<ScaleDrag>(*pivot, part, axisDir, startParam, pivotScreen, startScreenDist,
                                       boundOffset, std::move(apply));
}
