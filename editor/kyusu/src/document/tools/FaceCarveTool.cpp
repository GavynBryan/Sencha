#include "FaceCarveTool.h"

#include "ui/chrome/ChromeHeader.h"

#include "ui/ButtonFlow.h"
#include "ui/chrome/ChromeBars.h"
#include "ui/chrome/ChromeControls.h"
#include "ui/EditorUiStyle.h"

#include "EditorTheme.h"

#include "document/EditorScene.h"
#include "brush/BrushTransform.h"
#include "brush/BrushValidation.h"
#include "brush/CarveSurround.h"
#include "commands/CommandStack.h"
#include "meshedit/ManipulationSink.h"
#include "meshedit/MeshElements.h"
#include "meshedit/MeshEditService.h"
#include "overlay/EditorOverlayState.h"
#include "overlay/SelectionLabels.h"
#include "selection/SelectableRef.h"
#include "tools/ToolContext.h"
#include "viewport/EditorViewport.h"
#include "viewport/GridFrame.h"
#include "viewport/Picking.h"
#include "editmodes/GizmoMath.h"
#include "viewport/ViewportButtonMath.h"
#include "viewport/ViewportDialPlacement.h"
#include "viewport/ViewportProjection.h"

#include <algorithm>
#include <array>
#include <limits>
#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <vector>
#include <utility>

namespace
{
constexpr float kAdjustHandleHitPixels = 11.0f;

// How far a face may bow out of its own plane and still be a 2D workspace,
// relative to its size. Lives here rather than in the mesh layer, which has no
// business reading editor settings.
constexpr float kCarvePlanarTolerance = 1e-3f;

// What the mesh treats as coincident, which is what bounds how finely an arch
// can be sampled before its chords weld away.
constexpr float kCarveWeldTolerance = 1e-4f;

float ScreenDistance(ImVec2 a, ImVec2 b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

std::vector<SelectableRef> NewEdgeRefs(const EditorScene& scene, EntityId entity,
                                       const BrushMesh& original, const BrushMesh& edited)
{
    std::vector<bool> isNew(edited.Vertices.size(), false);
    for (std::size_t i = 0; i < edited.Vertices.size(); ++i)
    {
        const Vec3d p = edited.Vertices[i].Position;
        bool inOriginal = false;
        for (const BrushVertex& ov : original.Vertices)
        {
            if ((ov.Position - p).SqrMagnitude() <= 1.0e-8f)
            {
                inOriginal = true;
                break;
            }
        }
        isNew[i] = !inOriginal;
    }

    std::vector<SelectableRef> refs;
    const RegistryId registry = scene.GetRegistry().Id;
    for (const EdgeElement& edge : MeshElements::Edges(edited, Transform3f::Identity()))
    {
        if (edge.VertexA < isNew.size() && edge.VertexB < isNew.size()
            && isNew[edge.VertexA] && isNew[edge.VertexB])
            refs.push_back(SelectableRef::EdgeSelection(registry, entity, edge.Index));
    }
    return refs;
}

// Every edge of the faces the pierce made. Naming them beats inferring them
// from which vertices look new: a carve flush against the rim reuses vertices
// that were already there, so its springline-to-rim edges never look new.
std::vector<SelectableRef> TunnelEdgeRefs(const EditorScene& scene, EntityId entity,
                                          const BrushMesh& mesh,
                                          const std::vector<std::uint32_t>& walls)
{
    std::vector<std::pair<std::uint32_t, std::uint32_t>> wanted;
    for (std::uint32_t index : walls)
    {
        if (index >= mesh.Faces.size())
            continue;
        const std::vector<std::uint32_t>& loop = mesh.Faces[index].Loop;
        for (std::size_t i = 0; i < loop.size(); ++i)
        {
            const std::uint32_t a = loop[i];
            const std::uint32_t b = loop[(i + 1) % loop.size()];
            wanted.emplace_back(std::min(a, b), std::max(a, b));
        }
    }

    std::vector<SelectableRef> refs;
    const RegistryId registry = scene.GetRegistry().Id;
    for (const EdgeElement& edge : MeshElements::Edges(mesh, Transform3f::Identity()))
    {
        const std::pair<std::uint32_t, std::uint32_t> key{ std::min(edge.VertexA, edge.VertexB),
                                                           std::max(edge.VertexA, edge.VertexB) };
        if (std::find(wanted.begin(), wanted.end(), key) != wanted.end())
            refs.push_back(SelectableRef::EdgeSelection(registry, entity, edge.Index));
    }
    return refs;
}

// Forwards the drag to the tool. The ToolRegistry owns the tool and the
// InteractionHost cancels interactions before any tool switch, so the reference
// cannot dangle.
class CarveDragInteraction : public IInteraction
{
public:
    // What the press grabbed rides here rather than on the tool: a drag is
    // exactly this object's lifetime, so nothing has to remember to clear it.
    CarveDragInteraction(FaceCarveTool& tool, CarveHandleHit grabbed) : Tool(tool), Grabbed(grabbed) {}

    void OnPointerMove(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override
    {
        Tool.UpdateDrag(ctx, viewport, pointer.Position, Grabbed);
    }

    void OnPointerUp(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override
    {
        Tool.UpdateDrag(ctx, viewport, pointer.Position, Grabbed);
        Tool.EndDrag(ctx);
    }

    void OnCancel(ToolContext& ctx) override { Tool.RevertAll(ctx); }

private:
    FaceCarveTool& Tool;
    CarveHandleHit Grabbed;
};
}

std::string_view FaceCarveTool::GetId() const
{
    return "facecarve";
}

std::string_view FaceCarveTool::GetDisplayName() const
{
    return "Face Carve";
}

IconId FaceCarveTool::GetIcon() const
{
    return IconId::Carve;
}

void FaceCarveTool::SetMode(ToolContext& ctx, FaceCarveMode mode)
{
    if (Mode == mode)
        return;
    Mode = mode;
    RefreshPreview(ctx);
}

void FaceCarveTool::SetShape(ToolContext& ctx, CarveShape shape)
{
    if (Shape == shape)
        return;
    Shape = shape;
    RefreshPreview(ctx);
}

void FaceCarveTool::SetShapeParams(ToolContext& ctx, const CarveShapeParams& params)
{
    ShapeParams = params;
    RefreshPreview(ctx);
}

void FaceCarveTool::SetPierceEnabled(ToolContext& ctx, bool enabled)
{
    if (Pierce == enabled)
        return;
    Pierce = enabled;
    RefreshPreview(ctx);
}

CarveShapeLimits FaceCarveTool::ShapeLimits() const
{
    if (Phase == FaceCarvePhase::Idle)
        return CarveShapeRange(Vec2d{ 0.0f, 0.0f }, Vec2d{ 1.0f, 1.0f }, ShapeParams.ArchRise,
                               ShapeParams.Orientation, kCarveWeldTolerance);
    return CarveShapeRange(BoxMin(), BoxMax(), ShapeParams.ArchRise, ShapeParams.Orientation,
                           kCarveWeldTolerance);
}

Vec2d FaceCarveTool::BoxMin() const
{
    return Vec2d{ std::min(AnchorUv.X, LastUv.X), std::min(AnchorUv.Y, LastUv.Y) };
}

Vec2d FaceCarveTool::BoxMax() const
{
    return Vec2d{ std::max(AnchorUv.X, LastUv.X), std::max(AnchorUv.Y, LastUv.Y) };
}

void FaceCarveTool::RefreshPreview(ToolContext& ctx)
{
    if (Phase == FaceCarvePhase::Idle)
        return;
    const Vec2d boxMin = BoxMin();
    const Vec2d boxMax = BoxMax();
    if (Mode == FaceCarveMode::Quad)
        UpdateLoopPreview(ctx, boxMin, boxMax);
    else
        UpdatePolygonPreview(ctx, boxMin, boxMax);
    WriteReadout(ctx, boxMin, boxMax, Phase == FaceCarvePhase::Pending && PreviewValid);
}

InputConsumed FaceCarveTool::OnHover(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos)
{
    // A pending carve keeps its own glow and readout; hover only moves the
    // highlight between its confirm and cancel buttons.
    if (Phase != FaceCarvePhase::Idle)
    {
        ctx.Overlay.HoverBody = TargetEntity;
        HotButton = ButtonUnderCursor(viewport, pos);
        HotHandle = HotButton >= 0 ? CarveHandle::None : HandleUnderCursor(viewport, pos).Kind;
        WriteViewportButtons(ctx);
        WriteShapeHandles(ctx);
        WriteBoxHandles(ctx, BoxMin(), BoxMax());
        return InputConsumed::Yes;
    }
    HotButton = -1;
    HotHandle = CarveHandle::None;

    const SelectableRef picked = ctx.Picking.Pick(viewport, pos, ctx.Scene,
        BrushPickRequest{ .Mode = BrushPickMode::FaceOnly });
    if (!picked.IsFace())
    {
        ctx.Overlay.HoverBody = {};
        return InputConsumed::Yes;
    }

    const std::optional<MeshEditTargetMesh> resolved = ctx.Sink.ResolveMesh(picked.Entity);
    const char* refusal = nullptr;
    if (resolved.has_value() && resolved->Mesh != nullptr)
    {
        if (Mode == FaceCarveMode::Quad)
        {
            // Loop cuts propagate through quads; on anything else there is no
            // opposite edge to carry the cut to, which is a different
            // requirement from the carve's and says so in its own words.
            if (!BrushOps::RectFaceFrame(*resolved->Mesh, picked.ElementId).has_value())
                refusal = "quad mode needs a quad face";
        }
        else
        {
            const BrushFaceFrameResult frame =
                FaceFrame(*resolved->Mesh, picked.ElementId, kCarvePlanarTolerance);
            if (!frame.Frame.has_value())
                refusal = CarveStatusText(frame.Status);
        }
    }
    else
    {
        refusal = "no mesh here";
    }

    // Tools have no logging service; the reject reason surfaces as a readout
    // right where the user is pointing.
    ctx.Overlay.HoverBody = refusal == nullptr ? picked.Entity : EntityId{};
    if (refusal != nullptr && resolved.has_value() && resolved->Mesh != nullptr)
    {
        const auto face = MeshElements::TryGetFace(*resolved->Mesh, resolved->Transform, picked.ElementId);
        if (face.has_value())
        {
            DragReadout& readout = ctx.Overlay.Readout;
            readout.From = face->Center;
            readout.To = face->Center;
            readout.Text = refusal;
            readout.Viewport = viewport.Id;
        }
    }
    else
    {
        ctx.Overlay.Readout.Clear();
    }
    return InputConsumed::Yes;
}

void FaceCarveTool::OnHoverEnd(ToolContext& ctx)
{
    // The dispatcher fires HoverEnd on every pointer move while ANY interaction
    // is active: touch only the hover glow, and only when neither dragging nor
    // holding a pending carve (a revert here would destroy the live preview).
    if (Phase != FaceCarvePhase::Idle)
        return;
    ctx.Overlay.HoverBody = {};
    ctx.Overlay.Readout.Clear();
    ctx.Overlay.ViewportButtons.clear();
    ctx.Overlay.ViewportDials.clear();
}

InputConsumed FaceCarveTool::OnClick(ToolContext& ctx, EditorViewport& viewport,
                                     const PointerEvent& pointer)
{
    const int button = ButtonUnderCursor(viewport, pointer.Position);
    const ButtonRow row = BuildButtons();
    if (button >= 0 && button < static_cast<int>(row.Roles.size())
        && row.Buttons[static_cast<std::size_t>(button)].Enabled)
    {
        switch (row.Roles[static_cast<std::size_t>(button)])
        {
        case CarveButton::Confirm:
            Commit(ctx);
            return InputConsumed::Yes;
        case CarveButton::Cancel:
            RevertAll(ctx);
            return InputConsumed::Yes;
        case CarveButton::SegmentsDown:
        case CarveButton::SegmentsUp:
        {
            const CarveShapeLimits limits = ShapeLimits();
            CarveShapeParams params = ShapeParams;
            const int step = row.Roles[static_cast<std::size_t>(button)] == CarveButton::SegmentsUp
                                 ? 1 : -1;
            params.ArchSegments = std::clamp(params.ArchSegments + step, limits.MinSegments,
                                             limits.MaxSegments);
            SetShapeParams(ctx, params);
            return InputConsumed::Yes;
        }
        }
    }
    // A click is not a box; consuming it keeps stray clicks from re-selecting
    // while the tool is active, and a pending carve survives.
    return InputConsumed::Yes;
}

bool FaceCarveTool::CaptureFace(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos)
{
    if (Phase != FaceCarvePhase::Idle)
        RevertAll(ctx);

    const SelectableRef picked = ctx.Picking.Pick(viewport, pos, ctx.Scene,
        BrushPickRequest{ .Mode = BrushPickMode::FaceOnly });
    if (!picked.IsFace())
        return false;

    const std::optional<MeshEditTargetMesh> resolved = ctx.Sink.ResolveMesh(picked.Entity);
    if (!resolved.has_value() || resolved->Mesh == nullptr)
        return false;
    const BrushFaceFrameResult frame =
        FaceFrame(*resolved->Mesh, picked.ElementId, kCarvePlanarTolerance);
    if (!frame.Frame.has_value())
        return false;

    TargetEntity = picked.Entity;
    Original = *resolved->Mesh;
    FaceIndex = picked.ElementId;
    Frame = *frame.Frame;
    // Captured alongside, so switching to Quad or Pierce mid-edit does not
    // have to re-derive it and the box survives the switch.
    QuadFrame = BrushOps::RectFaceFrame(*resolved->Mesh, picked.ElementId);
    // The drag may roam the whole coplanar surface; the kernel decides whether
    // the shape actually lands on it.
    Workspace.clear();
    for (std::uint32_t index : CoplanarSurface(Original, FaceIndex, kCarvePlanarTolerance))
        Workspace.push_back(CornersOf(Original, index));
    FrameMin = Frame.Outline.front();
    FrameMax = Frame.Outline.front();
    for (const FaceCorners& face : Workspace)
        for (const Vec3d& corner : face.Corners)
        {
            const Vec2d p = Frame.ToFrame(corner);
            FrameMin.X = std::min(FrameMin.X, p.X);
            FrameMin.Y = std::min(FrameMin.Y, p.Y);
            FrameMax.X = std::max(FrameMax.X, p.X);
            FrameMax.Y = std::max(FrameMax.Y, p.Y);
        }
    TargetTransform = resolved->Transform;
    DragViewport = viewport.Id;

    // The snap plane in world space: the face's frame under the entity transform,
    // on the shared grid's spacing and phase. With non-uniform scale the world
    // axes lose orthogonality and the snap lattice is approximate; the snapped
    // point converts exactly back to local coordinates either way.
    DragPlane = GridFrame::SnapPlaneOnFace(
        TargetTransform.TransformPoint(Frame.Origin),
        TargetTransform.TransformVector(Frame.Normal).Normalized(),
        TargetTransform.TransformVector(Frame.AxisU).Normalized(),
        TargetTransform.TransformVector(Frame.AxisV).Normalized(), ctx.Grid);

    Phase = FaceCarvePhase::Dragging;
    PreviewValid = false;
    // ctx is the workspace-owned ToolContext every tool entry point receives,
    // so it outlives this scope; Commit and RevertAll close it before the tool
    // returns to Idle.
    ctx.Commands.OpenPendingEdit([this, &ctx] { RevertAll(ctx); });
    return true;
}

bool FaceCarveTool::BeginPendingAdjust(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos,
                                       int corner)
{
    if (Phase != FaceCarvePhase::Pending || !TargetEntity.IsValid() || corner < 0)
        return false;

    const std::optional<Vec2d> uv = CursorUv(ctx, viewport, pos);
    if (!uv.has_value())
        return false;

    // Grabbing a corner is a fresh corner-to-corner drag anchored at the one
    // opposite, which is why the corners need no identity past this point.
    const Vec2d boxMin = BoxMin();
    const Vec2d boxMax = BoxMax();
    const Vec2d corners[4] = {
        { boxMin.X, boxMin.Y },
        { boxMax.X, boxMin.Y },
        { boxMax.X, boxMax.Y },
        { boxMin.X, boxMax.Y },
    };
    AnchorUv = corners[static_cast<std::size_t>((corner + 2) % 4)];
    LastUv = *uv;
    Phase = FaceCarvePhase::Dragging;
    DragViewport = viewport.Id;
    ctx.Overlay.HoverBody = TargetEntity;
    return true;
}

bool FaceCarveTool::ShapeHandlesApply() const
{
    // They ride on a valid preview, like the corner handles. The shape means
    // the same thing in both modes: what the box is turned into.
    return Phase != FaceCarvePhase::Idle && PreviewValid && DragViewport.IsValid();
}

std::optional<ViewportDial::Placement> FaceCarveTool::Dial(const EditorViewport& viewport) const
{
    if (!ShapeHandlesApply())
        return std::nullopt;

    const Vec2d boxMin = BoxMin();
    const Vec2d boxMax = BoxMax();
    const Vec2d center{ (boxMin.X + boxMax.X) * 0.5f, (boxMin.Y + boxMax.Y) * 0.5f };
    const float semiMinor = std::min(boxMax.X - boxMin.X, boxMax.Y - boxMin.Y) * 0.5f;
    // The same circle the panel draws: it resolves the request through PlaceIn
    // too, so the ring under the cursor is the ring on screen.
    const ViewportDial::Placement placement = ViewportDial::PlaceIn(
        viewport, TargetTransform.TransformPoint(Frame.ToWorld(center)),
        TargetTransform.TransformVector(Frame.AxisU).Normalized(),
        TargetTransform.TransformVector(Frame.AxisV).Normalized(), semiMinor);
    return placement.Visible ? std::optional<ViewportDial::Placement>(placement) : std::nullopt;
}

Vec3d FaceCarveTool::RiseHandleWorld() const
{
    const CarveShapeFrame shape = CarveShapeFrameFor(BoxMin(), BoxMax(), ShapeParams.Orientation);
    const Vec2d springline = shape.ToBox(Vec2d{ 0.0f, shape.SpringlineAt(ShapeParams.ArchRise) });
    return TargetTransform.TransformPoint(Frame.ToWorld(springline));
}

float FaceCarveTool::TurnIncrement(const ToolContext& ctx) const
{
    // The toolbar's own snap toggle governs, rather than a second setting nobody
    // would think to look for. Off means free rotation, which SnapAngle spells
    // as an increment of zero.
    return ctx.Grid.SnapEnabled ? std::numbers::pi_v<float> / 4.0f : 0.0f;
}

CarveHandleHit FaceCarveTool::HandleUnderCursor(const EditorViewport& viewport, ImVec2 pos) const
{
    CarveHandleHit hit;
    if (Phase == FaceCarvePhase::Idle || !PreviewValid || !TargetEntity.IsValid())
        return hit;

    const ViewportProjection projection(viewport);
    float bestPixels = std::numeric_limits<float>::max();
    // Nearest in pixels wins. An exact tie falls to whichever was offered first,
    // which is a rule for determinism and not a preference between handles.
    const auto consider = [&](CarveHandle kind, int corner, float pixels, float tolerance) {
        if (pixels > tolerance || pixels >= bestPixels)
            return;
        bestPixels = pixels;
        hit = CarveHandleHit{ kind, corner };
    };
    const auto pixelsTo = [&](Vec3d world) -> std::optional<float> {
        const std::optional<ProjectedPoint> projected = projection.WorldToPixel(world);
        if (!projected.has_value())
            return std::nullopt;
        return ScreenDistance(pos, projected->Pixel);
    };

    if (ShapeHandlesApply() && Shape == CarveShape::Arch)
    {
        if (const std::optional<float> pixels = pixelsTo(RiseHandleWorld()))
            consider(CarveHandle::Rise, -1, *pixels, EditorUi::Px(ViewportDial::kKnobPixels));
    }

    if (const std::optional<ViewportDial::Placement> dial = Dial(viewport))
    {
        if (const std::optional<float> pixels = pixelsTo(dial->PointAt(ShapeParams.Orientation)))
            consider(CarveHandle::Orientation, -1, *pixels, EditorUi::Px(ViewportDial::kKnobPixels));

        // Anywhere on the ring turns it too, which is what a dial should allow.
        std::array<Vec3d, ViewportDial::kRimSegments + 1> ring{};
        const int count = ViewportDial::RimPoints(*dial, ring);
        std::vector<std::optional<ImVec2>> rim;
        rim.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            const std::optional<ProjectedPoint> projected =
                projection.WorldToPixel(ring[static_cast<std::size_t>(i)]);
            rim.push_back(projected.has_value() ? std::optional<ImVec2>(projected->Pixel)
                                                : std::nullopt);
        }
        if (const std::optional<float> pixels = ViewportDial::DistanceToRim(rim, pos))
            consider(CarveHandle::Orientation, -1, *pixels, EditorUi::Px(ViewportDial::kRimHitPixels));
    }

    const Vec2d boxMin = BoxMin();
    const Vec2d boxMax = BoxMax();
    const Vec2d corners[4] = {
        { boxMin.X, boxMin.Y },
        { boxMax.X, boxMin.Y },
        { boxMax.X, boxMax.Y },
        { boxMin.X, boxMax.Y },
    };
    for (int i = 0; i < 4; ++i)
    {
        const Vec3d world = TargetTransform.TransformPoint(Frame.ToWorld(corners[i]));
        if (const std::optional<float> pixels = pixelsTo(world))
            consider(CarveHandle::BoxCorner, i, *pixels, EditorUi::Px(kAdjustHandleHitPixels));
    }

    return hit;
}

void FaceCarveTool::DragOrientation(ToolContext& ctx, const EditorViewport& viewport, ImVec2 pos)
{
    const std::optional<ViewportDial::Placement> dial = Dial(viewport);
    if (!dial.has_value())
        return;
    const Ray3d ray = ViewportProjection(viewport).RayThroughPixel(pos);
    const std::optional<double> angle = GizmoMath::AngleOnPlane(
        ray, dial->Center, dial->AxisU.Cross(dial->AxisV), dial->AxisU, dial->AxisV);
    if (!angle.has_value())
        return;

    // The knob sits at the orientation, so it follows the cursor directly rather
    // than accumulating a delta; there is no wrap to unwind.
    CarveShapeParams params = ShapeParams;
    params.Orientation = static_cast<float>(GizmoMath::SnapAngle(*angle, TurnIncrement(ctx)));
    SetShapeParams(ctx, params);
}

void FaceCarveTool::DragRise(ToolContext& ctx, const EditorViewport& viewport, ImVec2 pos)
{
    const std::optional<Vec2d> uv = CursorUv(ctx, viewport, pos);
    if (!uv.has_value())
        return;

    const CarveShapeFrame shape = CarveShapeFrameFor(BoxMin(), BoxMax(), ShapeParams.Orientation);
    if (!(shape.SemiV > 0.0f))
        return;

    // Only the component along the shape's own rise axis survives, which is what
    // makes the handle a slider rather than a free point. The cursor arrived
    // already snapped to the face-plane lattice, and that is the only snap in the
    // gesture: the rise itself is never quantized a second time.
    const float along = shape.ToShape(*uv).Y;
    const CarveShapeLimits limits = ShapeLimits();
    CarveShapeParams params = ShapeParams;
    params.ArchRise = std::clamp((shape.SemiV - along) / (2.0f * shape.SemiV), limits.MinRise,
                                 limits.MaxRise);
    SetShapeParams(ctx, params);
}

bool FaceCarveTool::BoxInQuadFrame(Vec2d boxMin, Vec2d boxMax, Vec2d& outMin, Vec2d& outMax) const
{
    if (!QuadFrame.has_value())
        return false;

    // Both frames describe the same plane in brush-local space, so the corners
    // convert directly. Where the quad's own edges are not parallel to the
    // canonical axes the result is the box's span in the quad's frame, which is
    // the only thing loop cuts along quad edges can mean.
    const Vec2d corners[4] = {
        boxMin, Vec2d{ boxMax.X, boxMin.Y }, boxMax, Vec2d{ boxMin.X, boxMax.Y }
    };
    bool first = true;
    for (const Vec2d& corner : corners)
    {
        const Vec3d local = Frame.ToWorld(corner) - QuadFrame->Origin;
        const Vec2d quad{ local.Dot(QuadFrame->AxisU), local.Dot(QuadFrame->AxisV) };
        if (first)
        {
            outMin = quad;
            outMax = quad;
            first = false;
            continue;
        }
        outMin.X = std::min(outMin.X, quad.X);
        outMin.Y = std::min(outMin.Y, quad.Y);
        outMax.X = std::max(outMax.X, quad.X);
        outMax.Y = std::max(outMax.Y, quad.Y);
    }
    return true;
}

void FaceCarveTool::UpdatePolygonPreview(ToolContext& ctx, Vec2d boxMin, Vec2d boxMax)
{
    // Undo can revert the carve mid-drag; the live interaction keeps forwarding
    // moves, which must not resurrect a preview from stale capture state.
    if (Phase == FaceCarvePhase::Idle)
        return;

    const std::vector<Vec2d> outline = CarveShapeOutline(Shape, boxMin, boxMax, ShapeParams);
    PresentOutcome(ctx, CarveAcrossSurface(Original, Workspace, Frame, outline, Pierce, kCarvePlanarTolerance));
}

void FaceCarveTool::UpdateLoopPreview(ToolContext& ctx, Vec2d boxMin, Vec2d boxMax)
{
    if (Phase == FaceCarvePhase::Idle)
        return;

    Vec2d rectMin;
    Vec2d rectMax;
    if (!BoxInQuadFrame(boxMin, boxMax, rectMin, rectMax))
    {
        PresentOutcome(ctx, CarveOutcome::Failure(CarveStatus::TopologyFailure));
        return;
    }

    // The same shape the polygon carve would cut, laid into the cell the loop
    // cuts leave. The box itself lays nothing further and is the loop cuts alone.
    const std::vector<Vec2d> outline = CarveShapeOutline(Shape, boxMin, boxMax, ShapeParams);
    PresentOutcome(ctx, CarveWithinLoopBounds(Original, FaceIndex, *QuadFrame, rectMin, rectMax, Frame,
                                              outline, Pierce, kCarvePlanarTolerance));
}

void FaceCarveTool::PresentOutcome(ToolContext& ctx, CarveOutcome outcome)
{
    const auto refuse = [&](CarveStatus status) {
        LastStatus = status;
        ctx.Sink.PreviewMesh(TargetEntity, Original);
        PreviewValid = false;
    };
    if (!outcome.Ok())
    {
        refuse(outcome.Status());
        return;
    }

    CarveSuccess carved = outcome.Take();
    // The kernel owes a sound mesh. If repair has to change one, the carve is
    // not trustworthy and the face indices it just handed back are stale, so it
    // is refused rather than silently patched.
    if (BrushValidateAndRepair(carved.Mesh).Changed)
    {
        refuse(CarveStatus::TopologyFailure);
        return;
    }

    ctx.Sink.PreviewMesh(TargetEntity, carved.Mesh);
    Pending = std::move(carved.Mesh);
    PendingCutFaces = std::move(carved.CutFaces);
    PendingTunnelWalls = std::move(carved.TunnelWalls);
    PreviewValid = true;
    LastStatus = CarveStatus::Ok;
}

std::unique_ptr<IInteraction> FaceCarveTool::BeginDrag(ToolContext& ctx, EditorViewport& viewport,
                                                       const PointerEvent& pressPointer)
{
    if (ButtonUnderCursor(viewport, pressPointer.Position) >= 0)
        return nullptr; // the press belongs to a viewport button, not to a new carve

    const CarveHandleHit grabbed = HandleUnderCursor(viewport, pressPointer.Position);
    if (grabbed.Kind == CarveHandle::Rise || grabbed.Kind == CarveHandle::Orientation)
        return std::make_unique<CarveDragInteraction>(*this, grabbed);
    if (grabbed.Kind == CarveHandle::BoxCorner
        && BeginPendingAdjust(ctx, viewport, pressPointer.Position, grabbed.Corner))
        return std::make_unique<CarveDragInteraction>(*this, grabbed);

    if (!CaptureFace(ctx, viewport, pressPointer.Position))
        return nullptr;

    const std::optional<Vec2d> anchor = CursorUv(ctx, viewport, pressPointer.Position);
    if (!anchor.has_value())
    {
        RevertAll(ctx);
        return nullptr;
    }
    AnchorUv = *anchor;
    LastUv = *anchor;
    ctx.Overlay.HoverBody = TargetEntity;
    return std::make_unique<CarveDragInteraction>(*this, CarveHandleHit{ CarveHandle::BoxCorner, 0 });
}

void FaceCarveTool::UpdateDrag(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos,
                               CarveHandleHit grabbed)
{
    if (Phase == FaceCarvePhase::Idle)
        return;

    // A shape handle writes its own parameter and leaves the box alone; that is
    // the whole reason the press has to remember what it took hold of.
    switch (grabbed.Kind)
    {
    case CarveHandle::Orientation:
        DragOrientation(ctx, viewport, pos);
        break;
    case CarveHandle::Rise:
        DragRise(ctx, viewport, pos);
        break;
    case CarveHandle::None:
    case CarveHandle::BoxCorner:
    {
        const std::optional<Vec2d> uv = CursorUv(ctx, viewport, pos);
        if (uv.has_value())
            LastUv = *uv;
        // Always edit from the captured Original: the live mesh is the preview.
        RefreshPreview(ctx);
        break;
    }
    }
}

void FaceCarveTool::EndDrag(ToolContext& ctx)
{
    if (Phase != FaceCarvePhase::Dragging)
        return;
    if (!PreviewValid)
    {
        RevertAll(ctx);
        return;
    }
    Phase = FaceCarvePhase::Pending;
    // Release does NOT commit: the preview persists until Enter/Apply.
    WriteReadout(ctx, BoxMin(), BoxMax(), /*pending*/ true);
}

InputConsumed FaceCarveTool::OnKeyDown(ToolContext& ctx, const KeyDownEvent& event)
{
    if (Phase == FaceCarvePhase::Dragging)
        return InputConsumed::No;

    if (event.Key == SDLK_RETURN || event.Key == SDLK_KP_ENTER)
    {
        if (CanCommit())
        {
            Commit(ctx);
            return InputConsumed::Yes;
        }
        return InputConsumed::No;
    }
    if (event.Key == SDLK_ESCAPE)
    {
        if (Phase == FaceCarvePhase::Pending)
        {
            RevertAll(ctx);
            return InputConsumed::Yes;
        }
        return InputConsumed::No;
    }
    return InputConsumed::No;
}

void FaceCarveTool::OnDeactivate(ToolContext& ctx)
{
    RevertAll(ctx);
}

void FaceCarveTool::CommitPending(ToolContext& ctx)
{
    if (CanCommit())
        Commit(ctx);
    else
        RevertAll(ctx);
}

void FaceCarveTool::OnCancel(ToolContext& ctx)
{
    RevertAll(ctx);
}

void FaceCarveTool::Commit(ToolContext& ctx)
{
    if (!CanCommit() || !TargetEntity.IsValid())
        return;

    // Clear state into locals FIRST: CommitMesh and the tool hand-off re-enter
    // OnDeactivate -> RevertAll, which must then no-op.
    const EntityId entity = TargetEntity;
    const std::vector<std::uint32_t> cutFaces = std::move(PendingCutFaces);
    const std::vector<std::uint32_t> walls = std::move(PendingTunnelWalls);
    BrushMesh before = std::move(Original);
    BrushMesh after = std::move(Pending);
    ctx.Commands.ClosePendingEdit();
    Phase = FaceCarvePhase::Idle;
    PreviewValid = false;
    TargetEntity = {};
    ctx.Overlay.Readout.Clear();
    ctx.Overlay.HoverBody = {};
    ctx.Overlay.PointHandles.clear();
    ctx.Overlay.ViewportButtons.clear();
    ctx.Overlay.ViewportDials.clear();
    HotButton = -1;
    HotHandle = CarveHandle::None;

    if (!ctx.Sink.ResolveMesh(entity).has_value())
        return; // the entity died while pending; nothing to commit

    std::vector<SelectableRef> refs;
    MeshElementKind selectedKind = MeshElementKind::Face;
    if (!cutFaces.empty())
    {
        // The kernel names the opening, so the carve-then-extrude workflow no
        // longer depends on it happening to be the last face appended.
        for (std::uint32_t cutFace : cutFaces)
            refs.push_back(SelectableRef::FaceSelection(ctx.Scene.GetRegistry().Id, entity, cutFace));
    }
    else if (!walls.empty())
    {
        refs = TunnelEdgeRefs(ctx.Scene, entity, after, walls);
        selectedKind = MeshElementKind::Edge;
    }
    else
    {
        refs = NewEdgeRefs(ctx.Scene, entity, before, after);
        selectedKind = MeshElementKind::Edge;
    }

    ctx.Sink.CommitMesh(entity, std::move(before), std::move(after));

    ctx.Sink.SelectElements(refs);
    ctx.MeshEdit.SetElementKind(selectedKind);
    if (ctx.ActivateTool)
        ctx.ActivateTool("select");
}

void FaceCarveTool::RevertAll(ToolContext& ctx)
{
    if (Phase != FaceCarvePhase::Idle)
    {
        ctx.Commands.ClosePendingEdit();
        if (TargetEntity.IsValid() && ctx.Sink.ResolveMesh(TargetEntity).has_value())
            ctx.Sink.PreviewMesh(TargetEntity, Original);
    }
    Phase = FaceCarvePhase::Idle;
    PreviewValid = false;
    TargetEntity = {};
    ctx.Overlay.Readout.Clear();
    ctx.Overlay.HoverBody = {};
    ctx.Overlay.PointHandles.clear();
    ctx.Overlay.ViewportButtons.clear();
    ctx.Overlay.ViewportDials.clear();
    HotButton = -1;
    HotHandle = CarveHandle::None;
}

std::optional<Vec2d> FaceCarveTool::CursorUv(ToolContext& ctx, const EditorViewport& viewport,
                                             ImVec2 pos) const
{
    const std::optional<Vec3d> world = ctx.Picking.ProjectPointToPlane(viewport, pos, DragPlane);
    if (!world.has_value())
        return std::nullopt;

    // World -> local through the inverse transform (exact under non-uniform
    // scale), then frame coordinates, clamped onto the face.
    const Vec3d local = InverseTransformPoint(TargetTransform, *world);
    const Vec2d uv = Frame.ToFrame(local);
    // Clamped to the face's own extent rather than to a rectangle's width and
    // height, which an n-gon host does not have. A box inside the extent can
    // still leave a concave face; the kernel is what refuses that, and says so.
    return Vec2d{
        std::clamp(uv.X, FrameMin.X, FrameMax.X),
        std::clamp(uv.Y, FrameMin.Y, FrameMax.Y),
    };
}

void FaceCarveTool::WriteReadout(ToolContext& ctx, Vec2d boxMin, Vec2d boxMax, bool pending) const
{
    const auto worldAt = [&](Vec2d uv) { return TargetTransform.TransformPoint(Frame.ToWorld(uv)); };
    const Vec3d a = worldAt(boxMin);
    const Vec3d b = worldAt(boxMax);
    const Vec3d alongU = worldAt({ boxMax.X, boxMin.Y });

    DragReadout& readout = ctx.Overlay.Readout;
    readout.From = a;
    readout.To = b;
    // World extents measured between transformed corners, so scale reads truthfully.
    readout.Text = FormatUnits((alongU - a).Magnitude()) + " x " + FormatUnits((b - alongU).Magnitude());
    if (const int reached = FacesReached(boxMin, boxMax); reached > 1)
        readout.Text += "  across " + std::to_string(reached) + " faces";
    if (!PreviewValid && LastStatus != CarveStatus::Ok)
        readout.Text += "  ";
    if (!PreviewValid && LastStatus != CarveStatus::Ok)
        readout.Text += CarveStatusText(LastStatus);
    else if (pending)
        readout.Text += "  Enter to apply";
    readout.Viewport = DragViewport;
    WriteBoxHandles(ctx, boxMin, boxMax);
    WriteViewportButtons(ctx);
    WriteShapeHandles(ctx);
}

int FaceCarveTool::FacesReached(Vec2d boxMin, Vec2d boxMax) const
{
    const std::vector<Vec2d> outline = CarveShapeOutline(Shape, boxMin, boxMax, ShapeParams);
    int reached = 0;
    std::vector<Vec2d> region;
    for (const FaceCorners& face : Workspace)
    {
        region.clear();
        for (const Vec3d& corner : face.Corners)
            region.push_back(Frame.ToFrame(corner));
        if (PolygonsOverlap2D(outline, region, kCarveSnapTolerance))
            ++reached;
    }
    return reached;
}

void FaceCarveTool::WriteBoxHandles(ToolContext& ctx, Vec2d boxMin, Vec2d boxMax) const
{
    ctx.Overlay.PointHandles.clear();
    if (!PreviewValid || !DragViewport.IsValid())
        return;

    const Vec2d corners[4] = {
        { boxMin.X, boxMin.Y },
        { boxMax.X, boxMin.Y },
        { boxMax.X, boxMax.Y },
        { boxMin.X, boxMax.Y },
    };

    // A corner that has crossed onto another face of the surface takes the
    // accent, and so does every point where the shape's outline crosses the
    // picked face's rim: the line the user crossed is the thing that lights up.
    const std::span<const Vec2d> rim = Frame.Outline;
    for (Vec2d uv : corners)
    {
        PointHandleRequest handle;
        handle.World = TargetTransform.TransformPoint(Frame.ToWorld(uv));
        const bool beyond =
            ClassifyPointInPolygon2D(rim, uv, kCarveSnapTolerance) == PointPolygonRelation::Outside;
        handle.Fill = beyond ? EditorTheme::Hover : EditorTheme::Handle;
        handle.Border = EditorTheme::Readout;
        handle.SizePixels = EditorTheme::HandlePixels;
        ctx.Overlay.PointHandles.push_back(handle);
    }
    if (Mode == FaceCarveMode::Polygon)
    {
        const std::vector<Vec2d> outline = CarveShapeOutline(Shape, boxMin, boxMax, ShapeParams);
        for (std::size_t i = 0; i < outline.size(); ++i)
            for (std::size_t j = 0; j < rim.size(); ++j)
            {
                const std::optional<Vec2d> crossing =
                    SegmentCrossing2D(outline[i], outline[(i + 1) % outline.size()], rim[j],
                                      rim[(j + 1) % rim.size()], kCarveSnapTolerance);
                if (!crossing.has_value())
                    continue;
                PointHandleRequest tick;
                tick.World = TargetTransform.TransformPoint(Frame.ToWorld(*crossing));
                tick.Fill = EditorTheme::Hover;
                tick.Border = EditorTheme::Hover;
                tick.SizePixels = EditorTheme::HandlePixels * 0.5f;
                ctx.Overlay.PointHandles.push_back(tick);
            }
    }

    // The springline's own handle, in the accent so it does not read as a fifth
    // corner. It needs no viewport, unlike the dial, because it is just a point.
    if (ShapeHandlesApply() && Shape == CarveShape::Arch)
    {
        PointHandleRequest rise;
        rise.World = RiseHandleWorld();
        rise.Fill = HotHandle == CarveHandle::Rise ? EditorTheme::Hover : EditorTheme::Readout;
        rise.Border = EditorTheme::Handle;
        rise.SizePixels = EditorTheme::HandlePixels;
        ctx.Overlay.PointHandles.push_back(rise);
    }
}

FaceCarveTool::ButtonRow FaceCarveTool::BuildButtons() const
{
    ButtonRow row;
    // The segment stepper only when there is an arc to divide, and only when
    // some segment count is actually legal for this box and rise.
    if (Shape == CarveShape::Arch)
    {
        const CarveShapeLimits limits = ShapeLimits();
        if (!limits.Empty())
        {
            row.Buttons.push_back(
                ViewportButton{ IconId::None, "-", ShapeParams.ArchSegments > limits.MinSegments });
            row.Roles.push_back(CarveButton::SegmentsDown);
            row.Buttons.push_back(
                ViewportButton{ IconId::None, "+", ShapeParams.ArchSegments < limits.MaxSegments });
            row.Roles.push_back(CarveButton::SegmentsUp);
            // A readout rather than a third button: it says what the pair is
            // stepping, and nothing hit-tests it.
            row.Caption = ViewportButtonCaption{ std::to_string(ShapeParams.ArchSegments), 0, 1 };
        }
    }

    row.Buttons.push_back(ViewportButton{ IconId::Check, {}, CanCommit(), EditorChrome::ButtonTone::Active });
    row.Roles.push_back(CarveButton::Confirm);
    row.Buttons.push_back(ViewportButton{ IconId::Cancel, {}, true, EditorChrome::ButtonTone::Destructive });
    row.Roles.push_back(CarveButton::Cancel);
    return row;
}

void FaceCarveTool::WriteViewportButtons(ToolContext& ctx) const
{
    ctx.Overlay.ViewportButtons.clear();
    if (Phase == FaceCarvePhase::Idle || !DragViewport.IsValid())
        return;

    const Vec2d boxMin = BoxMin();
    const Vec2d boxMax = BoxMax();
    ViewportButtonRequest request;
    request.Anchors = {
        TargetTransform.TransformPoint(Frame.ToWorld(boxMin)),
        TargetTransform.TransformPoint(Frame.ToWorld(Vec2d{ boxMax.X, boxMin.Y })),
        TargetTransform.TransformPoint(Frame.ToWorld(boxMax)),
        TargetTransform.TransformPoint(Frame.ToWorld(Vec2d{ boxMin.X, boxMax.Y })),
    };
    ButtonRow row = BuildButtons();
    request.Buttons = std::move(row.Buttons);
    request.Caption = std::move(row.Caption);
    request.Hot = HotButton;
    request.Viewport = DragViewport;
    ctx.Overlay.ViewportButtons.push_back(std::move(request));
}

void FaceCarveTool::WriteShapeHandles(ToolContext& ctx) const
{
    ctx.Overlay.ViewportDials.clear();
    if (!ShapeHandlesApply())
        return;

    // Everything here is world geometry and tool state; only the radius needs a
    // camera, and the panel resolves that per frame. So this can be refreshed
    // wherever the preview is, and a turn typed into the properties panel moves
    // the knob immediately rather than waiting for the pointer to come back.
    const Vec2d boxMin = BoxMin();
    const Vec2d boxMax = BoxMax();
    const Vec2d center{ (boxMin.X + boxMax.X) * 0.5f, (boxMin.Y + boxMax.Y) * 0.5f };

    ViewportDialRequest request;
    request.Center = TargetTransform.TransformPoint(Frame.ToWorld(center));
    request.AxisU = TargetTransform.TransformVector(Frame.AxisU).Normalized();
    request.AxisV = TargetTransform.TransformVector(Frame.AxisV).Normalized();
    request.BoxSemiMinor = std::min(boxMax.X - boxMin.X, boxMax.Y - boxMin.Y) * 0.5f;
    request.Angle = ShapeParams.Orientation;
    request.TickIncrement = TurnIncrement(ctx);
    request.Hot = HotHandle == CarveHandle::Orientation;
    request.Viewport = DragViewport;
    ctx.Overlay.ViewportDials.push_back(request);
}

int FaceCarveTool::ButtonUnderCursor(const EditorViewport& viewport, ImVec2 pos) const
{
    if (Phase == FaceCarvePhase::Idle || viewport.Id != DragViewport)
        return -1;

    const Vec2d boxMin = BoxMin();
    const Vec2d boxMax = BoxMax();
    const Vec2d corners[4] = {
        boxMin, Vec2d{ boxMax.X, boxMin.Y }, boxMax, Vec2d{ boxMin.X, boxMax.Y }
    };
    const ViewportProjection projection(viewport);
    std::vector<std::optional<ImVec2>> anchors;
    anchors.reserve(4);
    for (const Vec2d& corner : corners)
    {
        const std::optional<ProjectedPoint> p =
            projection.WorldToPixel(TargetTransform.TransformPoint(Frame.ToWorld(corner)));
        anchors.push_back(p.has_value() ? std::optional<ImVec2>(p->Pixel) : std::nullopt);
    }
    const ViewportButtons::Row row =
        ViewportButtons::Layout(anchors, static_cast<int>(BuildButtons().Buttons.size()),
                                EditorUi::Px(1.0f), viewport.RegionMin, viewport.RegionMax);
    return row.HitTest(pos);
}

ITool::Shortcut FaceCarveTool::GetShortcut() const { return { SDLK_X, {} }; }

void FaceCarveTool::DrawProperties(ToolContext& ctx)
{
    EditorChrome::SectionTitle("Face Carve");

    const bool hasDraft = HasPending();
    const bool canCommit = CanCommit();

    if (ImGui::RadioButton("Polygon", Mode == FaceCarveMode::Polygon))
        SetMode(ctx, FaceCarveMode::Polygon);
    if (ImGui::RadioButton("Quad", Mode == FaceCarveMode::Quad))
        SetMode(ctx, FaceCarveMode::Quad);

    // The shape is what the box is turned into, in either mode: the polygon
    // carve cuts it out, the loop cuts lay it into the cell they leave.
    if (ImGui::RadioButton("Rectangle", Shape == CarveShape::Rectangle))
        SetShape(ctx, CarveShape::Rectangle);
    ImGui::SameLine();
    if (ImGui::RadioButton("Arch", Shape == CarveShape::Arch))
        SetShape(ctx, CarveShape::Arch);

    {
        // Degrees, because nobody authors a doorway in radians. The turn applies
        // to any shape: a quarter turn lays an arch on its side inside the same
        // opening, and it makes a rectangle a diamond off the axes.
        const float radiansPerDegree = std::numbers::pi_v<float> / 180.0f;
        float degrees = ShapeParams.Orientation / radiansPerDegree;
        if (ImGui::SliderFloat("Turn", &degrees, -180.0f, 180.0f, "%.0f deg"))
        {
            CarveShapeParams turned = ShapeParams;
            turned.Orientation = degrees * radiansPerDegree;
            SetShapeParams(ctx, turned);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Turns the shape inside the box; a quarter turn keeps the opening");
    }

    if (Shape == CarveShape::Arch)
    {
        const CarveShapeLimits limits = ShapeLimits();
        CarveShapeParams params = ShapeParams;
        if (ImGui::SliderFloat("Rise", &params.ArchRise, limits.MinRise, limits.MaxRise, "%.2f"))
            SetShapeParams(ctx, params);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Height of the arch as a fraction of the box; 1 springs from the floor");

        params = ShapeParams;
        if (limits.Empty())
        {
            ImGui::TextDisabled("Segments: no arc fits this rise");
        }
        else if (ImGui::SliderInt("Segments", &params.ArchSegments, limits.MinSegments,
                                  std::min(limits.MaxSegments, 64)))
        {
            params.ArchSegments = std::clamp(params.ArchSegments, limits.MinSegments, limits.MaxSegments);
            SetShapeParams(ctx, params);
        }
    }

    bool pierce = Pierce;
    if (ImGui::Checkbox("Pierce", &pierce))
        SetPierceEnabled(ctx, pierce);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Carve matching bounds into the nearest opposite face");

    if (!hasDraft)
    {
        ImGui::TextDisabled(Mode == FaceCarveMode::Quad ? "Drag a box on a quad face"
                                                            : "Drag a shape on a face");
    }
    if (Phase != FaceCarvePhase::Idle && !PreviewValid && LastStatus != CarveStatus::Ok)
        ImGui::TextDisabled("%s", CarveStatusText(LastStatus));

    if (!canCommit)
        ImGui::BeginDisabled();
    ButtonFlow flow;
    if (flow.Button("Apply", EditorChrome::ButtonTone::Active))
        Commit(ctx);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Commit the pending carve  [Enter]");
    if (!canCommit)
        ImGui::EndDisabled();
    if (!hasDraft)
        ImGui::BeginDisabled();
    if (flow.Button("Cancel"))
        RevertAll(ctx);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Discard the pending carve  [Esc]");
    if (!hasDraft)
        ImGui::EndDisabled();
}
