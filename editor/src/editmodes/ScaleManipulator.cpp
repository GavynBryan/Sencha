#include "ScaleManipulator.h"

#include "GizmoMath.h"
#include "ManipulatorTargets.h"
#include "SelectionPivot.h"
#include "../EditorTheme.h"
#include "../meshedit/ManipulationSink.h"
#include "../meshedit/MeshEditService.h"
#include "../overlay/EditorOverlayState.h"
#include "../tools/ToolContext.h"
#include "../viewport/EditorViewport.h"
#include "../viewport/ViewportProjection.h"

#include <math/geometry/3d/Ray3d.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <utility>
#include <vector>

namespace
{
constexpr float kStalkPixels = 80.0f;   // screen-constant axis length
constexpr float kBoxPixels = 6.0f;      // end-box half size
constexpr float kHitPixels = 10.0f;     // cursor-to-handle tolerance
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
    ElementScaleApply(EntityId entity, BrushMesh initial, Transform3f transform,
                      std::vector<SelectableRef> elements, MeshElementKind kind,
                      Vec3d pivot, MeshEditService& service, ManipulationSink& sink)
        : Entity(entity), Initial(std::move(initial)), Transform(transform)
        , Elements(std::move(elements)), Kind(kind), Pivot(pivot), Service(service), Sink(sink) {}

    void Preview(Vec3d factor) override
    {
        if (auto mesh = Service.ScaleElements(Initial, Transform, Elements, Kind, factor, Pivot, false))
            Sink.PreviewMesh(Entity, *mesh);
    }

    void Commit(Vec3d factor) override
    {
        if (auto mesh = Service.ScaleElements(Initial, Transform, Elements, Kind, factor, Pivot, true))
            Sink.CommitMesh(Entity, Initial, std::move(*mesh));
        else
            Sink.PreviewMesh(Entity, Initial);
    }

    void Cancel() override { Sink.PreviewMesh(Entity, Initial); }

private:
    EntityId Entity;
    BrushMesh Initial;
    Transform3f Transform;
    std::vector<SelectableRef> Elements;
    MeshElementKind Kind;
    Vec3d Pivot;
    MeshEditService& Service;
    ManipulationSink& Sink;
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
    std::optional<ElementTarget> r = ResolveElementTarget(ctx, kind);
    if (!r.has_value())
        return nullptr;
    return std::make_unique<ElementScaleApply>(
        r->Entity, std::move(r->Mesh), r->Transform, std::move(r->Elements), kind, pivot, ctx.Service, ctx.Sink);
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
              ImVec2 pivotScreen, float startScreenDist, std::unique_ptr<IScaleApply> apply)
        : Pivot(pivot), Part(part), AxisDir(axisDir), StartParam(startParam)
        , PivotScreen(pivotScreen), StartScreenDist(startScreenDist), Apply(std::move(apply)) {}

    void OnPointerMove(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override
    {
        const Vec3d factor = FactorAt(viewport, pointer.Position);
        Apply->Preview(factor);
        WriteReadout(ctx, viewport, factor);
    }

    void OnPointerUp(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override
    {
        Apply->Commit(FactorAt(viewport, pointer.Position));
        ctx.Overlay.Readout.Clear();
    }

    void OnCancel(ToolContext& ctx) override
    {
        Apply->Cancel();
        ctx.Overlay.Readout.Clear();
    }

private:
    Vec3d FactorAt(const EditorViewport& viewport, ImVec2 pos) const
    {
        if (Part >= 1 && Part <= 3)
        {
            const std::optional<double> s =
                GizmoMath::ClosestAxisParam(Pivot, AxisDir, ViewportProjection(viewport).RayThroughPixel(pos));
            if (!s.has_value() || std::abs(StartParam) < 1.0e-6)
                return Vec3d(1.0f, 1.0f, 1.0f);
            const float f = std::max(kMinFactor, static_cast<float>(*s / StartParam));
            Vec3d factor(1.0f, 1.0f, 1.0f);
            factor[Part - 1] = f;
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
    AppendBox(out, *pivot, projection.WorldSizeForPixels(*pivot, kBoxPixels), centerColor);
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
        if (toCenter <= bestPixels)
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
    ModifierFlags /*modifiers*/) const
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

    std::unique_ptr<IScaleApply> apply = (kind == MeshElementKind::Object)
        ? MakeObjectScaleApply(ctx, *pivot)
        : MakeElementScaleApply(ctx, kind, *pivot);
    if (apply == nullptr)
        return nullptr;

    return std::make_unique<ScaleDrag>(*pivot, part, axisDir, startParam, pivotScreen, startScreenDist, std::move(apply));
}
