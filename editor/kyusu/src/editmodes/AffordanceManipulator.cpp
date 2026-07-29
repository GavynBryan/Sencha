#include "AffordanceManipulator.h"

#include "AffordanceHandleMath.h"
#include "EditorTheme.h"
#include "GizmoMath.h"
#include "authoring/EditorComponentAdapter.h"
#include "tools/ToolContext.h"
#include "viewport/EditorViewport.h"
#include "viewport/ViewportProjection.h"

#include <math/spatial/GridPlane.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace
{
constexpr float kHitPixels = 11.0f;
constexpr float kHandlePixels = 7.0f;

double Get(const Vec3d& value, int axis)
{
    return axis == 0 ? value.X : axis == 1 ? value.Y : value.Z;
}

void Set(Vec3d& value, int axis, double coordinate)
{
    (axis == 0 ? value.X : axis == 1 ? value.Y : value.Z) =
        static_cast<float>(coordinate);
}

Vec3d UnitAxis(int axis)
{
    return axis == 0 ? Vec3d::Right()
        : axis == 1 ? Vec3d::Up() : -Vec3d::Forward();
}

struct AffordanceHandle
{
    enum class Kind : uint8_t { Aabb, Rectangle } Shape = Kind::Aabb;
    std::size_t Target = 0;
    int Axis = 0;
    bool Maximum = false;
    Vec3d Local;
    Vec3d World;
    Vec3d WorldAxis;
    double AxisScale = 1.0;
};

std::vector<AffordanceHandle> BuildHandles(const ViewportAffordanceOutput& output)
{
    std::vector<AffordanceHandle> handles;
    for (std::size_t target = 0; target < output.Aabbs.size(); ++target)
    {
        const AabbEditTarget& value = output.Aabbs[target];
        const Vec3d middle = (value.Value.Min + value.Value.Max) * 0.5f;
        for (int axis = 0; axis < 3; ++axis)
            for (bool maximum : { true, false })
            {
                Vec3d local = middle;
                Set(local, axis, maximum ? Get(value.Value.Max, axis)
                                          : Get(value.Value.Min, axis));
                const Vec3d vector = value.LocalToWorld.TransformVector(UnitAxis(axis));
                const double scale = vector.Magnitude();
                if (scale <= 1.0e-8)
                    continue;
                handles.push_back({ AffordanceHandle::Kind::Aabb, target, axis, maximum,
                                    local, value.LocalToWorld.TransformPoint(local),
                                    vector * static_cast<float>(1.0 / scale), scale });
            }
    }
    for (std::size_t target = 0; target < output.Rectangles.size(); ++target)
    {
        const RectEditTarget& value = output.Rectangles[target];
        for (int axis = 0; axis < 2; ++axis)
            for (bool maximum : { true, false })
            {
                Vec3d local{};
                Set(local, axis, (maximum ? 1.0 : -1.0)
                    * (axis == 0 ? value.HalfExtents.X : value.HalfExtents.Y));
                const Vec3d vector = value.LocalToWorld.TransformVector(UnitAxis(axis));
                const double scale = vector.Magnitude();
                if (scale <= 1.0e-8)
                    continue;
                handles.push_back({ AffordanceHandle::Kind::Rectangle, target, axis, maximum,
                                    local, value.LocalToWorld.TransformPoint(local),
                                    vector * static_cast<float>(1.0 / scale), scale });
            }
    }
    return handles;
}

void AppendCube(ManipulatorVisual& output, Vec3d center, float half, Vec4 color)
{
    const auto p = [&](int x, int y, int z)
    { return center + Vec3d(x * half, y * half, z * half); };
    const Vec3d c[8] = { p(-1,-1,-1), p(1,-1,-1), p(1,1,-1), p(-1,1,-1),
                         p(-1,-1,1), p(1,-1,1), p(1,1,1), p(-1,1,1) };
    static constexpr int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7},
    };
    for (const auto& edge : edges)
        output.Lines.push_back({ c[edge[0]], c[edge[1]], color });
}

class AabbDrag final : public IInteraction
{
public:
    AabbDrag(AabbEditTarget target, AffordanceHandle handle,
             std::unique_ptr<IValueEditTransaction<Aabb3d>> transaction)
        : Target(std::move(target)), H(handle), Transaction(std::move(transaction)) {}

    void OnPointerMove(ToolContext& context, EditorViewport& viewport,
                       const PointerEvent& pointer) override
    {
        if (const auto value = Compute(context, viewport, pointer))
            Transaction->Preview(*value);
    }
    void OnPointerUp(ToolContext& context, EditorViewport& viewport,
                     const PointerEvent& pointer) override
    {
        if (const auto value = Compute(context, viewport, pointer))
            Transaction->Commit(*value);
        else
            Transaction->Cancel();
    }
    void OnCancel(ToolContext&) override { Transaction->Cancel(); }

private:
    std::optional<Aabb3d> Compute(ToolContext& context, EditorViewport& viewport,
                                  const PointerEvent& pointer) const
    {
        const Ray3d ray = ViewportProjection(viewport).RayThroughPixel(pointer.Position);
        const auto parameter = GizmoMath::ClosestAxisParam(H.World, H.WorldAxis, ray);
        if (!parameter)
            return std::nullopt;
        double worldOffset = *parameter;
        const GridPlane grid = viewport.GetGrid(context.Grid);
        if (grid.SnapEnabled)
            worldOffset = GizmoMath::SnapAxisOffset(
                worldOffset, H.World.Dot(H.WorldAxis),
                grid.Origin.Dot(H.WorldAxis), grid.Spacing);
        const double coordinate = Get(H.Local, H.Axis) + worldOffset / H.AxisScale;
        return AffordanceHandleMath::ResizeAabbFace(
            Target.Value, H.Axis, H.Maximum, coordinate);
    }

    AabbEditTarget Target;
    AffordanceHandle H;
    std::unique_ptr<IValueEditTransaction<Aabb3d>> Transaction;
};

class RectangleDrag final : public IInteraction
{
public:
    RectangleDrag(RectEditTarget target, AffordanceHandle handle,
                  std::unique_ptr<IValueEditTransaction<RectangleEditValue>> transaction)
        : Target(std::move(target)), H(handle), Transaction(std::move(transaction)) {}

    void OnPointerMove(ToolContext& context, EditorViewport& viewport,
                       const PointerEvent& pointer) override
    { if (const auto value = Compute(context, viewport, pointer)) Transaction->Preview(*value); }
    void OnPointerUp(ToolContext& context, EditorViewport& viewport,
                     const PointerEvent& pointer) override
    {
        if (const auto value = Compute(context, viewport, pointer))
            Transaction->Commit(*value);
        else
            Transaction->Cancel();
    }
    void OnCancel(ToolContext&) override { Transaction->Cancel(); }

private:
    std::optional<RectangleEditValue> Compute(
        ToolContext& context, EditorViewport& viewport,
        const PointerEvent& pointer) const
    {
        const Ray3d ray = ViewportProjection(viewport).RayThroughPixel(pointer.Position);
        const auto parameter = GizmoMath::ClosestAxisParam(H.World, H.WorldAxis, ray);
        if (!parameter)
            return std::nullopt;
        double worldOffset = *parameter;
        const GridPlane grid = viewport.GetGrid(context.Grid);
        if (grid.SnapEnabled)
            worldOffset = GizmoMath::SnapAxisOffset(
                worldOffset, H.World.Dot(H.WorldAxis),
                grid.Origin.Dot(H.WorldAxis), grid.Spacing);
        const double edge = Get(H.Local, H.Axis) + worldOffset / H.AxisScale;
        const auto bounds = AffordanceHandleMath::ResizeRectangleEdge(
            Target.HalfExtents, H.Axis, H.Maximum, edge);
        if (!bounds)
            return std::nullopt;
        return RectangleEditValue{
            .CenterOffset = bounds->Center(),
            .HalfExtents = bounds->HalfExtent(),
        };
    }

    RectEditTarget Target;
    AffordanceHandle H;
    std::unique_ptr<IValueEditTransaction<RectangleEditValue>> Transaction;
};
}

bool AffordanceManipulator::AppliesTo(const ManipulatorContext& context,
                                      const EditorViewport&) const
{
    return context.Affordances != nullptr && context.Affordances->HasEditTargets();
}

void AffordanceManipulator::BuildVisual(const ManipulatorContext& context,
                                        const EditorViewport& viewport,
                                        int hoveredPart,
                                        ManipulatorVisual& output) const
{
    if (context.Affordances == nullptr)
        return;
    ViewportAffordanceOutput affordances;
    context.Affordances->Build(affordances);
    const std::vector<AffordanceHandle> handles = BuildHandles(affordances);
    const ViewportProjection projection(viewport);
    for (std::size_t i = 0; i < handles.size(); ++i)
    {
        const Vec4 color = static_cast<int>(i) + 1 == hoveredPart
            ? EditorTheme::Hover : EditorTheme::Handle;
        AppendCube(output, handles[i].World,
                   projection.WorldSizeForPixels(handles[i].World, kHandlePixels) * 0.5f,
                   color);
    }
}

int AffordanceManipulator::HitTest(const ManipulatorContext& context,
                                   const EditorViewport& viewport,
                                   ImVec2 screen) const
{
    if (context.Affordances == nullptr)
        return 0;
    ViewportAffordanceOutput affordances;
    context.Affordances->Build(affordances);
    const std::vector<AffordanceHandle> handles = BuildHandles(affordances);
    const ViewportProjection projection(viewport);
    int best = 0;
    float bestDistance = kHitPixels;
    for (std::size_t i = 0; i < handles.size(); ++i)
    {
        const auto point = projection.WorldToPixel(handles[i].World);
        if (!point)
            continue;
        const float dx = screen.x - point->Pixel.x;
        const float dy = screen.y - point->Pixel.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (distance <= bestDistance)
        {
            bestDistance = distance;
            best = static_cast<int>(i) + 1;
        }
    }
    return best;
}

std::unique_ptr<IInteraction> AffordanceManipulator::BeginDrag(
    int part, const ManipulatorContext& context, const EditorViewport&,
    ImVec2, ModifierFlags) const
{
    if (context.Affordances == nullptr || part <= 0)
        return nullptr;
    ViewportAffordanceOutput affordances;
    context.Affordances->Build(affordances);
    const std::vector<AffordanceHandle> handles = BuildHandles(affordances);
    if (static_cast<std::size_t>(part) > handles.size())
        return nullptr;
    const AffordanceHandle handle = handles[static_cast<std::size_t>(part - 1)];
    if (handle.Shape == AffordanceHandle::Kind::Aabb)
    {
        AabbEditTarget target = affordances.Aabbs[handle.Target];
        auto transaction = target.BeginEdit ? target.BeginEdit() : nullptr;
        return transaction ? std::make_unique<AabbDrag>(
            std::move(target), handle, std::move(transaction)) : nullptr;
    }
    RectEditTarget target = affordances.Rectangles[handle.Target];
    auto transaction = target.BeginEdit ? target.BeginEdit() : nullptr;
    return transaction ? std::make_unique<RectangleDrag>(
        std::move(target), handle, std::move(transaction)) : nullptr;
}
