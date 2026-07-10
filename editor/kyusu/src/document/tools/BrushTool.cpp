#include "BrushTool.h"

#include "fonts/IconsFontAwesome6.h"

#include "brush/BrushMesh.h"
#include "brush/BrushOps.h"
#include "commands/CommandStack.h"
#include "document/BrushCreationSettings.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/commands/DeferredCreateBrushCommand.h"
#include "document/interactions/BrushCreateDragInteraction.h"
#include "document/interactions/BrushCreationPlane.h"
#include "meshedit/ActiveMaterialState.h"
#include "meshedit/MeshEditService.h"
#include "render/PreviewBuffer.h"
#include "selection/commands/SelectCommand.h"
#include "selection/SelectionService.h"
#include "tools/ToolContext.h"
#include "viewport/GridSettings.h"
#include "viewport/Picking.h"

#include <SDL2/SDL_keycode.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <span>
#include <utility>

namespace
{
    // The local-space mesh for the pending brush's current settings. Placement
    // lives on the scene entity; this builds shape only.
    BrushMesh BuildPendingMesh(const BrushCreationSettings& settings,
                               const BrushTool::PendingBrush& pending,
                               float depthHalf)
    {
        BrushPrimitiveParams params{};
        params.HalfExtents = pending.HalfExtents;
        params.HalfExtents[pending.DepthAxis] = depthHalf;
        params.DepthAxis = pending.DepthAxis;
        params.CylinderSides = settings.CylinderSides;
        params.PlaneSubdivisions = settings.PlaneSubdivisions;

        BrushMesh mesh = BrushOps::MakePrimitive(settings.ActivePrimitive, params);
        if (settings.Inner)
            mesh = BrushOps::FlipAllFaces(mesh);
        return mesh;
    }

    // World direction of the pending brush's local depth axis under a transform.
    Vec3d PendingDepthDir(const Quatf& rotation, int depthAxis)
    {
        Vec3d local{};
        local[depthAxis] = 1.0f;
        return rotation.RotateVector(local);
    }

    void MeshLocalBounds(const BrushMesh& mesh, Vec3d& outMin, Vec3d& outMax)
    {
        outMin = Vec3d{};
        outMax = Vec3d{};
        if (mesh.Vertices.empty())
            return;
        outMin = mesh.Vertices.front().Position;
        outMax = outMin;
        for (const BrushVertex& vertex : mesh.Vertices)
            for (int a = 0; a < 3; ++a)
            {
                outMin[a] = std::min(outMin[a], vertex.Position[a]);
                outMax[a] = std::max(outMax[a], vertex.Position[a]);
            }
    }

    bool BoundsNearlyEqual(Vec3d aMin, Vec3d aMax, Vec3d bMin, Vec3d bMax)
    {
        constexpr float kBoundsTol = 1e-4f;
        for (int a = 0; a < 3; ++a)
            if (std::abs(aMin[a] - bMin[a]) > kBoundsTol || std::abs(aMax[a] - bMax[a]) > kBoundsTol)
                return false;
        return true;
    }

    void ApplyActiveMaterial(BrushMesh& mesh, const AssetRef& material)
    {
        if (!material.IsValid())
            return;
        for (BrushFace& face : mesh.Faces)
            face.Material.Material = material;
    }
}

std::string_view BrushTool::GetId() const
{
    return "brush";
}

std::string_view BrushTool::GetDisplayName() const
{
    return "Brush";
}

std::string_view BrushTool::GetIcon() const
{
    return ICON_FA_CUBE;
}

void BrushTool::OnActivate(ToolContext& ctx)
{
    // Brush creation works on whole entities. A face/edge/vertex mode left
    // active from the Select tool would keep element picking and gizmo
    // behavior in effect, so drop back to Object.
    if (ctx.MeshEdit.GetElementKind() != MeshElementKind::Object)
    {
        ctx.Selection.ClearMeshElementSelections();
        ctx.MeshEdit.SetElementKind(MeshElementKind::Object);
    }
}

void BrushTool::OnDeactivate(ToolContext& ctx)
{
    // Switching tools is a click-off: the pending brush becomes real.
    CommitPending(ctx);
}

void BrushTool::OnCancel(ToolContext& ctx)
{
    CancelPending(ctx);
}

InputConsumed BrushTool::OnClick(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer)
{
    // Clicking off a pending brush commits it; the click is spent on the commit
    // rather than also changing the selection underneath it.
    if (State == BrushToolState::Pending)
    {
        CommitPending(ctx);
        return InputConsumed::Yes;
    }

    const SelectableRef picked = ctx.Picking.Pick(
        viewport, pointer.Position, ctx.Scene, BrushPickRequest{ .Mode = BrushPickMode::EntityOnly });
    if (picked.IsValid() && ctx.Scene.TryGetBrush(picked.Entity) != nullptr)
        ctx.Commands.Execute(std::make_unique<SelectCommand>(ctx.Selection, picked));
    else
        ctx.Commands.Execute(std::make_unique<SelectCommand>(ctx.Selection, SelectableRef{}));
    return InputConsumed::Yes;
}

InputConsumed BrushTool::OnKeyDown(ToolContext& ctx, const KeyDownEvent& event)
{
    if (State != BrushToolState::Pending)
        return InputConsumed::No;

    if (event.Key == SDLK_RETURN || event.Key == SDLK_KP_ENTER)
    {
        CommitPending(ctx);
        return InputConsumed::Yes;
    }
    if (event.Key == SDLK_ESCAPE)
    {
        CancelPending(ctx);
        return InputConsumed::Yes;
    }
    return InputConsumed::No;
}

std::unique_ptr<IInteraction> BrushTool::BeginDrag(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pressPointer)
{
    // Dragging off a pending brush commits it first, then starts the next one.
    CommitPending(ctx);

    // Selection is a click; a drag always creates, including inside an existing
    // brush's bounds. The resolver decides the plane and depth (rest on grid,
    // copy a selected brush's off-axis size, or rest on the surface under the
    // cursor in perspective).
    const std::optional<BrushCreationPlane> plane =
        ResolveBrushCreationPlane(ctx, viewport, pressPointer.Position);
    if (!plane.has_value())
        return nullptr;

    return std::make_unique<BrushCreateDragInteraction>(*plane, *this);
}

void BrushTool::SetPending(ToolContext& ctx, const PendingBrush& pending)
{
    State = BrushToolState::Pending;
    Pending = pending;
    Pending.DepthFloorHalf = ctx.Grid.Spacing * 0.5f;

    const bool solid = ctx.BrushCreate.ActivePrimitive != BrushPrimitive::Plane;
    const float depthHalf = PendingDepthHalf(solid, Pending.DragDepthHalf, Pending.DepthFloorHalf);
    Pending.LastBuiltDepthHalf = depthHalf;

    Transform3f transform = Transform3f::Identity();
    transform.Rotation = Pending.Rotation;
    // The drag centered the brush for its own depth half; building with a
    // different half keeps the rest side fixed by shifting along the depth
    // direction instead of growing symmetrically.
    transform.Position = Pending.Center
        + PendingDepthDir(Pending.Rotation, Pending.DepthAxis)
              * (Pending.DepthSign * (depthHalf - Pending.DragDepthHalf));

    const std::span<const SelectableRef> selection = ctx.Selection.GetSelection();
    PendingPreviousSelection.assign(selection.begin(), selection.end());

    BrushMesh mesh = BuildPendingMesh(ctx.BrushCreate, Pending, depthHalf);
    MeshLocalBounds(mesh, Pending.LastBuiltBoundsMin, Pending.LastBuiltBoundsMax);
    ApplyActiveMaterial(mesh, ctx.ActiveMaterial.Active);
    PendingMaterial = ctx.ActiveMaterial.Active;
    PendingEntity = ctx.Scene.CreateBrushFromMesh(transform, std::move(mesh));
    ctx.Preview.Clear();
    ctx.Selection.SetSelection({
        SelectableRef::EntitySelection(ctx.Scene.GetRegistry().Id, PendingEntity),
    });
    // ctx is the workspace-owned ToolContext every tool entry point receives,
    // so it outlives this scope; the scope itself is closed by CommitPending or
    // CancelPending before the tool leaves Pending.
    ctx.Commands.OpenPendingEdit([this, &ctx] { CancelPending(ctx); });
}

void BrushTool::RefreshPending(ToolContext& ctx)
{
    if (State != BrushToolState::Pending)
        return;
    if (!PendingEntity.IsValid())
        return;
    const BrushMesh* liveMesh = ctx.Scene.TryGetBrushMesh(PendingEntity);
    const Transform3f* live = ctx.Scene.TryGetTransform(PendingEntity);
    if (liveMesh == nullptr || live == nullptr)
        return;

    // The live transform owns placement (gizmo edits to the pending brush
    // stick), and the live mesh's bounds own the extents: external mesh edits
    // (bounds handles, face drags) are adopted into the generation parameters
    // before regenerating, not wiped. The entity recenters on the adopted
    // bounds because generated primitives are origin-centered.
    Transform3f moved = *live;
    bool moveEntity = false;

    Vec3d liveMin;
    Vec3d liveMax;
    MeshLocalBounds(*liveMesh, liveMin, liveMax);
    if (!BoundsNearlyEqual(liveMin, liveMax, Pending.LastBuiltBoundsMin, Pending.LastBuiltBoundsMax))
    {
        const Vec3d center = (liveMin + liveMax) * 0.5f;
        for (int a = 0; a < 3; ++a)
            Pending.HalfExtents[a] = (liveMax[a] - liveMin[a]) * 0.5f;
        Pending.DragDepthHalf = Pending.HalfExtents[Pending.DepthAxis];
        Pending.LastBuiltDepthHalf = Pending.DragDepthHalf;
        if (center.SqrMagnitude() > 0.0f)
        {
            moved.Position = moved.TransformPoint(center);
            moveEntity = true;
        }
    }

    const bool solid = ctx.BrushCreate.ActivePrimitive != BrushPrimitive::Plane;
    const float depthHalf = PendingDepthHalf(solid, Pending.DragDepthHalf, Pending.DepthFloorHalf);

    BrushMesh mesh = BuildPendingMesh(ctx.BrushCreate, Pending, depthHalf);
    MeshLocalBounds(mesh, Pending.LastBuiltBoundsMin, Pending.LastBuiltBoundsMax);
    ApplyActiveMaterial(mesh, ctx.ActiveMaterial.Active);
    PendingMaterial = ctx.ActiveMaterial.Active;

    // Only a primitive-class depth change moves the entity beyond the adopted
    // recenter, and relatively, so the rest side stays where the drag put it.
    if (depthHalf != Pending.LastBuiltDepthHalf)
    {
        moved.Position += PendingDepthDir(moved.Rotation, Pending.DepthAxis)
            * (Pending.DepthSign * (depthHalf - Pending.LastBuiltDepthHalf)
               * moved.Scale[Pending.DepthAxis]);
        Pending.LastBuiltDepthHalf = depthHalf;
        moveEntity = true;
    }
    if (moveEntity)
        ctx.Scene.SetTransform(PendingEntity, moved);
    ctx.Scene.SetBrushMesh(PendingEntity, std::move(mesh));
}

bool BrushTool::PendingMaterialStale(const ToolContext& ctx) const
{
    return State == BrushToolState::Pending
        && ctx.ActiveMaterial.Active.IsValid()
        && ctx.ActiveMaterial.Active.Path != PendingMaterial.Path;
}

void BrushTool::CommitPending(ToolContext& ctx)
{
    if (State != BrushToolState::Pending)
        return;
    if (!PendingEntity.IsValid() || ctx.Scene.TryGetBrushMesh(PendingEntity) == nullptr)
    {
        ctx.Commands.ClosePendingEdit();
        State = BrushToolState::Idle;
        Pending = {};
        PendingEntity = {};
        PendingPreviousSelection.clear();
        return;
    }

    const EntityId entity = PendingEntity;
    EntitySnapshot snapshot = ctx.Document.CaptureEntity(entity);
    std::vector<SelectableRef> beforeSelection = std::move(PendingPreviousSelection);
    ctx.Commands.ClosePendingEdit();
    State = BrushToolState::Idle;
    Pending = {};
    PendingEntity = {};
    PendingPreviousSelection.clear();
    ctx.Preview.Clear();
    ctx.Commands.Execute(std::make_unique<DeferredCreateBrushCommand>(
        entity, std::move(snapshot), ctx.Scene, ctx.Document, ctx.Selection, std::move(beforeSelection)));
}

void BrushTool::CancelPending(ToolContext& ctx)
{
    if (State != BrushToolState::Pending)
        return;
    ctx.Commands.ClosePendingEdit();
    State = BrushToolState::Idle;
    Pending = {};
    if (PendingEntity.IsValid())
        ctx.Scene.DestroyEntity(PendingEntity);
    PendingEntity = {};
    ctx.Preview.Clear();
    ctx.Selection.SetSelection(std::move(PendingPreviousSelection));
    PendingPreviousSelection.clear();
}
