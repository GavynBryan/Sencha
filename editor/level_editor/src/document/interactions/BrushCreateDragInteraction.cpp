#include "BrushCreateDragInteraction.h"

#include "document/tools/BrushTool.h"
#include "brush/BrushMesh.h"
#include "brush/BrushOps.h"
#include "document/tools/BrushTool.h"
#include "render/PreviewBuffer.h"
#include "tools/ToolContext.h"
#include "viewport/EditorViewport.h"
#include "viewport/Picking.h"

#include <cmath>
#include <memory>
#include <utility>

BrushCreateDragInteraction::BrushCreateDragInteraction(BrushCreationPlane plane, BrushTool& tool)
    : Plane(plane)
    , LastCenter(plane.Anchor)
    , LastHalfExtents(Vec3d(0.5f, 0.5f, 0.5f))
    , Tool(tool)
{
}

int BrushCreateDragInteraction::AxisIndex(Vec3d axis)
{
    if (std::abs(axis.X) > 0.5f) return 0;
    if (std::abs(axis.Y) > 0.5f) return 1;
    return 2;
}

namespace
{
    // The live drag preview mesh plus the transform placing it. Built with the
    // same settings BrushTool uses for the pending brush, so the preview does
    // not change shape on release.
    std::pair<Transform3f, BrushMesh> BuildPrimitive(const BrushCreationSettings& settings,
                                                     int depthAxis, Vec3d center, Vec3d halfExtents,
                                                     Quatf rotation = Quatf::Identity())
    {
        BrushPrimitiveParams params{};
        params.HalfExtents = halfExtents;
        params.DepthAxis = depthAxis;
        params.CylinderSides = settings.CylinderSides;
        params.PlaneSubdivisions = settings.PlaneSubdivisions;

        Transform3f transform = Transform3f::Identity();
        transform.Position = center;
        transform.Rotation = rotation;

        BrushMesh mesh = BrushOps::MakePrimitive(settings.ActivePrimitive, params);
        if (settings.Inner)
            mesh = BrushOps::FlipAllFaces(mesh);
        return { transform, std::move(mesh) };
    }
}

void BrushCreateDragInteraction::UpdatePreview(ToolContext& ctx, Vec3d snapped)
{
    const float spacing = ctx.Grid.Spacing;
    const float minHalf = spacing * 0.5f;

    if (!Plane.FrameAligned)
    {
        // Custom grid frame: measure the drag in plane UV space and rotate the
        // brush onto the frame so it aligns with the working grid. Depth runs on
        // local Y, which the rotation maps onto the frame normal.
        const Vec3d u = Plane.Plane.AxisU;
        const Vec3d v = Plane.Plane.AxisV;
        const Vec3d drag = snapped - Plane.Anchor;
        const float du = drag.Dot(u);
        const float dv = drag.Dot(v);

        Vec3d halfExtents{};
        halfExtents.X = std::max(std::abs(du) * 0.5f, minHalf);
        halfExtents.Y = Plane.DepthHalf;
        halfExtents.Z = std::max(std::abs(dv) * 0.5f, minHalf);

        const Vec3d center = Plane.Anchor + u * (du * 0.5f) + v * (dv * 0.5f)
                           + Plane.DepthDir * (Plane.DepthCenter - Plane.Anchor.Dot(Plane.DepthDir));

        HasValidSize = (std::abs(du) >= spacing || std::abs(dv) >= spacing);
        LastCenter = center;
        LastHalfExtents = halfExtents;
        // Local X -> U, local Z -> V; local Y -> V x U completes the right-handed
        // basis (the depth extent is symmetric, so its sign is irrelevant).
        LastRotation = Quatf::FromBasis(u, v.Cross(u), v);

        auto [transform, mesh] = BuildPrimitive(Tool.Creation, /*depthAxis*/ 1, center, halfExtents, LastRotation);
        ctx.Preview.SetMesh(transform, std::move(mesh));
        return;
    }

    const int uIdx = AxisIndex(Plane.Plane.AxisU);
    const int vIdx = AxisIndex(Plane.Plane.AxisV);
    const int dIdx = Plane.DepthAxis;

    Vec3d halfExtents{};
    halfExtents[uIdx] = std::max(std::abs(snapped[uIdx] - Plane.Anchor[uIdx]) * 0.5f, minHalf);
    halfExtents[vIdx] = std::max(std::abs(snapped[vIdx] - Plane.Anchor[vIdx]) * 0.5f, minHalf);
    halfExtents[dIdx] = Plane.DepthHalf;

    Vec3d center{};
    center[uIdx] = (Plane.Anchor[uIdx] + snapped[uIdx]) * 0.5f;
    center[vIdx] = (Plane.Anchor[vIdx] + snapped[vIdx]) * 0.5f;
    center[dIdx] = Plane.DepthCenter;

    const float dragU = std::abs(snapped[uIdx] - Plane.Anchor[uIdx]);
    const float dragV = std::abs(snapped[vIdx] - Plane.Anchor[vIdx]);
    HasValidSize = (dragU >= spacing || dragV >= spacing);

    LastCenter = center;
    LastHalfExtents = halfExtents;
    LastRotation = Quatf::Identity();

    auto [transform, mesh] = BuildPrimitive(Tool.Creation, Plane.DepthAxis, center, halfExtents);
    ctx.Preview.SetMesh(transform, std::move(mesh));
}

void BrushCreateDragInteraction::OnPointerMove(ToolContext& ctx,
                                               EditorViewport& viewport,
                                               const PointerEvent& pointer)
{
    const std::optional<Vec3d> snapped = ctx.Picking.ProjectPointToPlane(viewport, pointer.Position, Plane.Plane);
    if (!snapped.has_value())
        return;

    UpdatePreview(ctx, *snapped);
}

void BrushCreateDragInteraction::OnPointerUp(ToolContext& ctx,
                                             EditorViewport& viewport,
                                             const PointerEvent& pointer)
{
    const std::optional<Vec3d> snapped = ctx.Picking.ProjectPointToPlane(viewport, pointer.Position, Plane.Plane);
    if (snapped.has_value())
        UpdatePreview(ctx, *snapped);

    if (!HasValidSize)
    {
        ctx.Preview.Clear();
        return;
    }

    // Release does not commit: the drag result becomes the tool's pending brush,
    // which the Tool Properties panel can keep reshaping (primitive kind, sides,
    // subdivisions, inner) until Apply, Enter, a click-off, or a new drag.
    BrushTool::PendingBrush pending{};
    pending.Center = LastCenter;
    pending.HalfExtents = LastHalfExtents;
    pending.Rotation = LastRotation;
    pending.DepthAxis = Plane.FrameAligned ? Plane.DepthAxis : 1;
    pending.DragDepthHalf = Plane.DepthHalf;
    // The resolver's rest sign is measured along DepthDir; the pending brush
    // stores it along its local depth axis, which the rotated (frame) basis
    // maps to -DepthDir, so re-sign it against the actual local axis.
    Vec3d localDepth{};
    localDepth[pending.DepthAxis] = 1.0f;
    const Vec3d localDepthWorld = LastRotation.RotateVector(localDepth);
    pending.DepthSign = localDepthWorld.Dot(Plane.DepthDir) < 0.0f ? -Plane.DepthSign
                                                                   : Plane.DepthSign;
    Tool.SetPending(ctx, pending);
}

void BrushCreateDragInteraction::OnCancel(ToolContext& ctx)
{
    ctx.Preview.Clear();
    HasValidSize = false;
}
