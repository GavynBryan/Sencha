#include "SpaceDragInteraction.h"

#include "brush/BrushOps.h"
#include "document/tools/SpaceTool.h"
#include "render/PreviewBuffer.h"
#include "tools/ToolContext.h"
#include "viewport/Picking.h"

#include <cmath>

namespace
{
    constexpr float kSmallest = 0.1f;

    bool Sized(const Vec3d& a, const Vec3d& b)
    {
        return std::abs(a.X - b.X) >= kSmallest && std::abs(a.Z - b.Z) >= kSmallest;
    }
}

SpaceDragInteraction::SpaceDragInteraction(GridPlane floor, Vec3d anchor, float height, SpaceTool& tool)
    : Floor(floor)
    , Anchor(anchor)
    , Corner(anchor)
    , Height(height)
    , Tool(tool)
{
}

void SpaceDragInteraction::Preview(ToolContext& ctx)
{
    if (!Sized(Anchor, Corner))
    {
        ctx.Preview.Clear();
        return;
    }
    Transform3f transform = Transform3f::Identity();
    transform.Position = Vec3d{ (Anchor.X + Corner.X) * 0.5f, Anchor.Y + Height * 0.5f, (Anchor.Z + Corner.Z) * 0.5f };
    ctx.Preview.SetMesh(transform, BrushOps::MakeBox(Vec3d{ std::abs(Corner.X - Anchor.X) * 0.5f, Height * 0.5f,
                                                            std::abs(Corner.Z - Anchor.Z) * 0.5f }));
}

void SpaceDragInteraction::OnPointerMove(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer)
{
    if (const std::optional<Vec3d> snapped = ctx.Picking.ProjectPointToPlane(viewport, pointer.Position, Floor))
        Corner = *snapped;
    Preview(ctx);
}

void SpaceDragInteraction::OnPointerUp(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer)
{
    OnPointerMove(ctx, viewport, pointer);
    ctx.Preview.Clear();
    if (Sized(Anchor, Corner))
        Tool.Create(ctx, Anchor, Corner);
}

void SpaceDragInteraction::OnCancel(ToolContext& ctx)
{
    ctx.Preview.Clear();
}
