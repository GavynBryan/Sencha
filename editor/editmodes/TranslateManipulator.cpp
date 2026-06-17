#include "TranslateManipulator.h"

#include "GizmoMath.h"
#include "SelectionPivot.h"
#include "../EditorTheme.h"
#include "../meshedit/ManipulationSink.h"
#include "../meshedit/MeshEditService.h"
#include "../tools/ToolContext.h"
#include "../viewport/EditorViewport.h"
#include "../viewport/ViewportProjection.h"

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

Vec3d AxisDirection(int axis)
{
    switch (axis)
    {
    case kAxisX: return Vec3d(1.0f, 0.0f, 0.0f);
    case kAxisY: return Vec3d(0.0f, 1.0f, 0.0f);
    case kAxisZ: return Vec3d(0.0f, 0.0f, 1.0f);
    default:     return Vec3d(0.0f, 0.0f, 0.0f);
    }
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

SelectableKind ElementSelectableKind(MeshElementKind kind)
{
    switch (kind)
    {
    case MeshElementKind::Vertex: return SelectableKind::Vertex;
    case MeshElementKind::Edge:   return SelectableKind::Edge;
    case MeshElementKind::Face:   return SelectableKind::Face;
    case MeshElementKind::Object:
    default:                      return SelectableKind::Entity;
    }
}

// Applies a translation delta to whatever is being manipulated, via the sink.
struct ITranslateApply
{
    virtual void Preview(Vec3d delta) = 0;
    virtual void Commit(Vec3d delta) = 0;
    virtual void Cancel() = 0;
    virtual ~ITranslateApply() = default;
};

class ObjectApply : public ITranslateApply
{
public:
    struct Item
    {
        EntityId Entity;
        Transform3f Initial;
    };

    ObjectApply(std::vector<Item> items, ManipulationSink& sink)
        : Items(std::move(items)), Sink(sink) {}

    void Preview(Vec3d delta) override
    {
        for (const Item& item : Items)
            Sink.PreviewTransform(item.Entity, WithDelta(item.Initial, delta));
    }

    void Commit(Vec3d delta) override
    {
        std::vector<TransformEdit> edits;
        edits.reserve(Items.size());
        for (const Item& item : Items)
            edits.push_back({ item.Entity, item.Initial, WithDelta(item.Initial, delta) });
        Sink.CommitTransforms(edits);
    }

    void Cancel() override
    {
        for (const Item& item : Items)
            Sink.PreviewTransform(item.Entity, item.Initial);
    }

private:
    [[nodiscard]] static Transform3f WithDelta(const Transform3f& initial, Vec3d delta)
    {
        Transform3f result = initial;
        result.Position += delta;
        return result;
    }

    std::vector<ObjectApply::Item> Items;
    ManipulationSink& Sink;
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

class TranslateDrag : public IInteraction
{
public:
    TranslateDrag(Vec3d pivot, Vec3d axisDir, double startParam,
                  std::unique_ptr<ITranslateApply> apply)
        : Pivot(pivot), AxisDir(axisDir), StartParam(startParam), Apply(std::move(apply)) {}

    void OnPointerMove(ToolContext&, EditorViewport& viewport, const PointerEvent& pointer) override
    {
        if (UpdateDelta(viewport, pointer.Position))
            Apply->Preview(LastDelta);
    }

    void OnPointerUp(ToolContext&, EditorViewport& viewport, const PointerEvent& pointer) override
    {
        UpdateDelta(viewport, pointer.Position);
        Apply->Commit(LastDelta);
    }

    void OnCancel(ToolContext&) override { Apply->Cancel(); }

private:
    bool UpdateDelta(const EditorViewport& viewport, ImVec2 pos)
    {
        const std::optional<double> s =
            GizmoMath::ClosestAxisParam(Pivot, AxisDir, ViewportProjection(viewport).RayThroughPixel(pos));
        if (!s.has_value())
            return false;
        const GridPlane grid = viewport.GetGrid();
        const double offset = GizmoMath::SnapAxisOffset(
            *s - StartParam, Pivot.Dot(AxisDir), grid.Origin.Dot(AxisDir), grid.Spacing);
        LastDelta = AxisDir * static_cast<float>(offset);
        return true;
    }

    Vec3d Pivot;
    Vec3d AxisDir;
    double StartParam;
    Vec3d LastDelta = {};
    std::unique_ptr<ITranslateApply> Apply;
};

// Picks the entity to edit (primary's if it matches the wanted kind, else the
// first matching ref) and collects that entity's refs of that kind.
EntityId GatherModeElements(const SelectionSnapshot& selection, SelectableKind wantKind,
                            std::vector<SelectableRef>& outElements)
{
    EntityId entity = {};
    if (selection.Primary.IsValid() && selection.Primary.Kind == wantKind)
        entity = selection.Primary.Entity;
    else
    {
        for (SelectableRef ref : selection.Items)
            if (ref.IsValid() && ref.Kind == wantKind) { entity = ref.Entity; break; }
    }
    if (!entity.IsValid())
        return {};
    for (SelectableRef ref : selection.Items)
        if (ref.IsValid() && ref.Kind == wantKind && ref.Entity == entity)
            outElements.push_back(ref);
    return entity;
}

std::unique_ptr<ITranslateApply> MakeObjectApply(const ManipulatorContext& ctx)
{
    // Move every selected entity that resolves to a transform — one drag, one
    // undo, all of them.
    std::vector<ObjectApply::Item> items;
    for (SelectableRef ref : ctx.Selection.Items)
    {
        if (!ref.IsEntity())
            continue;
        if (const std::optional<Transform3f> transform = ctx.Sink.ResolveTransform(ref.Entity))
            items.push_back({ ref.Entity, *transform });
    }
    if (items.empty())
        return nullptr;
    return std::make_unique<ObjectApply>(std::move(items), ctx.Sink);
}

std::unique_ptr<ITranslateApply> MakeElementApply(const ManipulatorContext& ctx, MeshElementKind kind)
{
    std::vector<SelectableRef> elements;
    const EntityId entity = GatherModeElements(ctx.Selection, ElementSelectableKind(kind), elements);
    if (!entity.IsValid() || elements.empty())
        return nullptr;
    const std::optional<MeshEditTargetMesh> resolved = ctx.Sink.ResolveMesh(entity);
    if (!resolved.has_value() || resolved->Mesh == nullptr)
        return nullptr;
    return std::make_unique<ElementApply>(
        entity, *resolved->Mesh, resolved->Transform, std::move(elements), kind, ctx.Service, ctx.Sink);
}
}

bool TranslateManipulator::AppliesTo(const ManipulatorContext& ctx, const EditorViewport&) const
{
    return ComputeSelectionPivot(ctx.Sink, ctx.Selection, ctx.Service.GetElementKind()).has_value();
}

void TranslateManipulator::BuildVisual(const ManipulatorContext& ctx,
                                       const EditorViewport& viewport,
                                       int hoveredPart,
                                       ManipulatorVisual& out) const
{
    const std::optional<Vec3d> pivot =
        ComputeSelectionPivot(ctx.Sink, ctx.Selection, ctx.Service.GetElementKind());
    if (!pivot.has_value())
        return;

    const float length = AxisLength(viewport, *pivot);
    const float headLength = length * 0.18f;
    const float headRadius = headLength * 0.45f;

    const std::array<int, 3> axes = { kAxisX, kAxisY, kAxisZ };
    const std::array<Vec4, 3> colors = { EditorTheme::AxisX, EditorTheme::AxisY, EditorTheme::AxisZ };

    for (std::size_t i = 0; i < axes.size(); ++i)
    {
        const Vec3d dir = AxisDirection(axes[i]);
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
    const std::optional<Vec3d> pivot =
        ComputeSelectionPivot(ctx.Sink, ctx.Selection, ctx.Service.GetElementKind());
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
            projection.WorldToPixel(*pivot + AxisDirection(axis) * length);
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
    ImVec2 screenPos) const
{
    const Vec3d axisDir = AxisDirection(part);
    if (part == 0 || axisDir.SqrMagnitude() == 0.0f)
        return nullptr;

    const MeshElementKind kind = ctx.Service.GetElementKind();
    const std::optional<Vec3d> pivot = ComputeSelectionPivot(ctx.Sink, ctx.Selection, kind);
    if (!pivot.has_value())
        return nullptr;

    const std::optional<double> startParam =
        GizmoMath::ClosestAxisParam(*pivot, axisDir, ViewportProjection(viewport).RayThroughPixel(screenPos));
    if (!startParam.has_value())
        return nullptr;

    std::unique_ptr<ITranslateApply> apply =
        (kind == MeshElementKind::Object) ? MakeObjectApply(ctx) : MakeElementApply(ctx, kind);
    if (apply == nullptr)
        return nullptr;

    return std::make_unique<TranslateDrag>(*pivot, axisDir, *startParam, std::move(apply));
}
