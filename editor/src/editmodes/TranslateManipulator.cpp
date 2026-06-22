#include "TranslateManipulator.h"

#include "GizmoMath.h"
#include "SelectionPivot.h"
#include "../EditorTheme.h"
#include "../meshedit/ManipulationSink.h"
#include "../meshedit/MeshEditService.h"
#include "../meshedit/MeshElementKindTraits.h"
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

// Applies a translation delta to whatever is being manipulated, via the sink.
struct ITranslateApply
{
    virtual void Preview(Vec3d delta) = 0;
    virtual void Commit(Vec3d delta) = 0;
    virtual void Cancel() = 0;
    virtual ~ITranslateApply() = default;
};

// One selected entity and its pre-drag transform. Shared by the move and the
// Shift-drag duplicate of object mode.
struct ObjectItem
{
    EntityId Entity;
    Transform3f Initial;
};

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

class TranslateDrag : public IInteraction
{
public:
    TranslateDrag(Vec3d pivot, Vec3d axisDir, double startParam,
                  std::unique_ptr<ITranslateApply> apply)
        : Pivot(pivot), AxisDir(axisDir), StartParam(startParam), Apply(std::move(apply)) {}

    void OnPointerMove(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override
    {
        if (UpdateDelta(ctx, viewport, pointer.Position))
            Apply->Preview(LastDelta);
    }

    void OnPointerUp(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override
    {
        UpdateDelta(ctx, viewport, pointer.Position);
        Apply->Commit(LastDelta);
    }

    void OnCancel(ToolContext&) override { Apply->Cancel(); }

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

// Every selected entity that resolves to a transform, with its pre-drag state.
std::vector<ObjectItem> GatherObjectItems(const ManipulatorContext& ctx)
{
    std::vector<ObjectItem> items;
    for (SelectableRef ref : ctx.Selection.Items)
    {
        if (!ref.IsEntity())
            continue;
        if (const std::optional<Transform3f> transform = ctx.Sink.ResolveTransform(ref.Entity))
            items.push_back({ ref.Entity, *transform });
    }
    return items;
}

std::unique_ptr<ITranslateApply> MakeObjectApply(const ManipulatorContext& ctx)
{
    // Move every selected entity — one drag, one undo, all of them.
    std::vector<ObjectItem> items = GatherObjectItems(ctx);
    if (items.empty())
        return nullptr;
    return std::make_unique<ObjectApply>(std::move(items), ctx.Sink);
}

std::unique_ptr<ITranslateApply> MakeDuplicateApply(const ManipulatorContext& ctx)
{
    std::vector<ObjectItem> items = GatherObjectItems(ctx);
    if (items.empty())
        return nullptr;
    return std::make_unique<DuplicateApply>(std::move(items), ctx.Sink);
}

// Resolves the active-mode element selection to one entity, its mesh, transform,
// and the refs of that kind. Shared by the element move and the extrude.
struct ResolvedElements
{
    EntityId Entity;
    BrushMesh Mesh;
    Transform3f Transform;
    std::vector<SelectableRef> Elements;
};

std::optional<ResolvedElements> ResolveElements(const ManipulatorContext& ctx, MeshElementKind kind)
{
    std::vector<SelectableRef> elements;
    const EntityId entity = GatherModeElements(ctx.Selection, Traits(kind).Selectable, elements);
    if (!entity.IsValid() || elements.empty())
        return std::nullopt;
    const std::optional<MeshEditTargetMesh> resolved = ctx.Sink.ResolveMesh(entity);
    if (!resolved.has_value() || resolved->Mesh == nullptr)
        return std::nullopt;
    return ResolvedElements{ entity, *resolved->Mesh, resolved->Transform, std::move(elements) };
}

std::unique_ptr<ITranslateApply> MakeElementApply(const ManipulatorContext& ctx, MeshElementKind kind)
{
    std::optional<ResolvedElements> r = ResolveElements(ctx, kind);
    if (!r.has_value())
        return nullptr;
    return std::make_unique<ElementApply>(
        r->Entity, std::move(r->Mesh), r->Transform, std::move(r->Elements), kind, ctx.Service, ctx.Sink);
}

std::unique_ptr<ITranslateApply> MakeExtrudeApply(const ManipulatorContext& ctx, MeshElementKind kind)
{
    std::optional<ResolvedElements> r = ResolveElements(ctx, kind);
    if (!r.has_value())
        return nullptr;
    return std::make_unique<ExtrudeApply>(
        r->Entity, std::move(r->Mesh), r->Transform, std::move(r->Elements), kind, ctx.Service, ctx.Sink);
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
    ImVec2 screenPos,
    ModifierFlags modifiers) const
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

    // Shift turns the drag into duplicate (object) or extrude (face/edge); vertex
    // mode and the plain drag stay a move. Extrude falls back to a move if no
    // face/edge refs resolve, so Shift never dead-ends the drag.
    std::unique_ptr<ITranslateApply> apply;
    if (kind == MeshElementKind::Object)
        apply = modifiers.Shift ? MakeDuplicateApply(ctx) : MakeObjectApply(ctx);
    else if (modifiers.Shift && (kind == MeshElementKind::Face || kind == MeshElementKind::Edge))
        apply = MakeExtrudeApply(ctx, kind);
    if (apply == nullptr)
        apply = (kind == MeshElementKind::Object) ? MakeObjectApply(ctx) : MakeElementApply(ctx, kind);
    if (apply == nullptr)
        return nullptr;

    return std::make_unique<TranslateDrag>(*pivot, axisDir, *startParam, std::move(apply));
}
