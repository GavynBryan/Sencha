#include "ClipTool.h"

#include "ui/chrome/ChromeHeader.h"
#include "ui/chrome/ChromeBars.h"
#include "ui/chrome/ChromeControls.h"
#include "ui/EditorUiStyle.h"

#include "EditorTheme.h"

#include "brush/BrushOps.h"
#include "brush/BrushValidation.h"
#include "commands/CommandStack.h"
#include "document/EditorScene.h"
#include "meshedit/ManipulationSink.h"
#include "overlay/EditorOverlayState.h"
#include "overlay/SelectionLabels.h"
#include "render/PreviewBuffer.h"
#include "selection/SelectableRef.h"
#include "selection/SelectionService.h"
#include "tools/ToolContext.h"
#include "viewport/ClipPlaneMath.h"
#include "viewport/EditorViewport.h"
#include "viewport/GridFrame.h"
#include "viewport/GridSettings.h"
#include "viewport/Picking.h"
#include "viewport/ViewportButtonMath.h"
#include "viewport/ViewportOrientation.h"
#include "viewport/ViewportProjection.h"

#include <imgui.h>
#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

namespace
{
enum class ClipButton : std::uint8_t
{
    Front,
    Back,
    Split,
    Cap,
    Confirm,
    Cancel,
};

const char* ModeTooltip(ClipMode mode)
{
    switch (mode)
    {
    case ClipMode::KeepFront: return "Keep front  [Tab]";
    case ClipMode::KeepBack:  return "Keep back  [Tab]";
    case ClipMode::Split:     return "Split into two brushes  [Tab]";
    }
    return "";
}

IconId ModeIcon(ClipMode mode)
{
    switch (mode)
    {
    case ClipMode::KeepFront: return IconId::ClipFront;
    case ClipMode::KeepBack:  return IconId::ClipBack;
    case ClipMode::Split:     return IconId::ClipSplit;
    }
    return IconId::None;
}

constexpr const char* kCapTooltip = "Close the cut with a face";

// Forwards the drag to the tool. The ToolRegistry owns the tool and the
// InteractionHost cancels interactions before any tool switch, so the reference
// cannot dangle. Which endpoint the press grabbed rides here: a drag is exactly
// this object's lifetime.
class ClipDragInteraction : public IInteraction
{
public:
    ClipDragInteraction(ClipTool& tool, int endpoint) : Tool(tool), Endpoint(endpoint) {}

    void OnPointerMove(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override
    {
        Tool.UpdateDrag(ctx, viewport, pointer.Position, Endpoint);
    }

    void OnPointerUp(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override
    {
        Tool.UpdateDrag(ctx, viewport, pointer.Position, Endpoint);
        Tool.EndDrag(ctx, viewport);
    }

    void OnCancel(ToolContext& ctx) override { Tool.RevertAll(ctx); }

private:
    ClipTool& Tool;
    int Endpoint;
};

// Two in-plane axes for a plane with this normal, from the world axis least
// aligned with it, so a snap lattice on a picked face has a stable frame.
void InPlaneAxes(Vec3d normal, Vec3d& u, Vec3d& v)
{
    const Vec3d n = normal.Normalized();
    const Vec3d seed = std::abs(n.X) < 0.9f ? Vec3d{ 1, 0, 0 } : Vec3d{ 0, 1, 0 };
    u = n.Cross(seed).Normalized();
    v = n.Cross(u).Normalized();
}

// Appends `mesh` placed by `transform` into `into`, in world space, so several
// brushes' discarded halves draw as one wireframe.
void AppendInWorld(BrushMesh& into, const BrushMesh& mesh, const Transform3f& transform)
{
    const std::uint32_t base = static_cast<std::uint32_t>(into.Vertices.size());
    for (const BrushVertex& vertex : mesh.Vertices)
        into.Vertices.push_back(BrushVertex{ transform.TransformPoint(vertex.Position) });
    for (const BrushFace& face : mesh.Faces)
    {
        BrushFace shifted = face;
        for (std::uint32_t& index : shifted.Loop)
            index += base;
        into.Faces.push_back(std::move(shifted));
    }
}

const char* ModeText(ClipMode mode)
{
    switch (mode)
    {
    case ClipMode::KeepFront: return "keep front";
    case ClipMode::KeepBack:  return "keep back";
    case ClipMode::Split:     return "split";
    }
    return "";
}
}

struct ClipTool::ButtonRow
{
    std::vector<ViewportButton> Buttons;
    std::vector<ClipButton> Roles;
};

std::string_view ClipTool::GetId() const { return "clip"; }
std::string_view ClipTool::GetDisplayName() const { return "Clip"; }
IconId ClipTool::GetIcon() const { return IconId::Clip; }
ITool::Shortcut ClipTool::GetShortcut() const { return { SDLK_K, {} }; }

void ClipTool::SetMode(ToolContext& ctx, ClipMode mode)
{
    if (Mode == mode)
        return;
    Mode = mode;
    if (Phase != ClipPhase::Idle)
        RefreshPreview(ctx);
}

void ClipTool::SetCapped(ToolContext& ctx, bool capped)
{
    if (Capped == capped)
        return;
    Capped = capped;
    if (Phase != ClipPhase::Idle)
        RefreshPreview(ctx);
}

ClipTool::ButtonRow ClipTool::BuildButtons() const
{
    ButtonRow row;
    const auto toggle = [&](IconId icon, const char* tooltip, bool on, ClipButton role) {
        ViewportButton button{ icon, {}, true,
                               on ? EditorChrome::ButtonTone::Active : EditorChrome::ButtonTone::Normal,
                               tooltip };
        row.Buttons.push_back(std::move(button));
        row.Roles.push_back(role);
    };
    for (const ClipMode mode : { ClipMode::KeepFront, ClipMode::KeepBack, ClipMode::Split })
        toggle(ModeIcon(mode), ModeTooltip(mode), Mode == mode,
               mode == ClipMode::KeepFront ? ClipButton::Front
               : mode == ClipMode::KeepBack ? ClipButton::Back : ClipButton::Split);
    toggle(IconId::ClipCap, kCapTooltip, Capped, ClipButton::Cap);
    ViewportButton confirm{ IconId::Check, {}, CanCommit(), EditorChrome::ButtonTone::Active, "Apply  [Enter]" };
    row.Buttons.push_back(std::move(confirm));
    row.Roles.push_back(ClipButton::Confirm);
    ViewportButton cancel{ IconId::Cancel, {}, true, EditorChrome::ButtonTone::Destructive, "Cancel  [Esc]" };
    row.Buttons.push_back(std::move(cancel));
    row.Roles.push_back(ClipButton::Cancel);
    return row;
}

int ClipTool::ButtonUnderCursor(const EditorViewport& viewport, ImVec2 pos) const
{
    if (Phase != ClipPhase::Pending || viewport.Id != SourceViewport)
        return -1;
    const ViewportProjection projection(viewport);
    std::vector<std::optional<ImVec2>> anchors;
    for (const Vec3d& world : { A, B })
    {
        const std::optional<ProjectedPoint> p = projection.WorldToPixel(world);
        anchors.push_back(p.has_value() ? std::optional<ImVec2>(p->Pixel) : std::nullopt);
    }
    const ViewportButtons::Row row =
        ViewportButtons::Layout(anchors, static_cast<int>(BuildButtons().Buttons.size()),
                                EditorUi::Px(1.0f), viewport.RegionMin, viewport.RegionMax);
    return row.HitTest(pos);
}

int ClipTool::HandleUnderCursor(const EditorViewport& viewport, ImVec2 pos) const
{
    if (Phase != ClipPhase::Pending || viewport.Id != SourceViewport)
        return -1;
    const ViewportProjection projection(viewport);
    const float reach = EditorTheme::HandlePixels;
    int best = -1;
    float bestDistance = reach;
    const Vec3d ends[2] = { A, B };
    for (int i = 0; i < 2; ++i)
    {
        const std::optional<ProjectedPoint> p = projection.WorldToPixel(ends[i]);
        if (!p.has_value())
            continue;
        const float distance = std::hypot(p->Pixel.x - pos.x, p->Pixel.y - pos.y);
        if (distance <= bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

InputConsumed ClipTool::OnHover(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos)
{
    if (Phase != ClipPhase::Pending)
        return InputConsumed::No;
    const int button = ButtonUnderCursor(viewport, pos);
    const int handle = button >= 0 ? -1 : HandleUnderCursor(viewport, pos);
    if (button != HotButton || handle != HotHandle)
    {
        HotButton = button;
        HotHandle = handle;
        WriteOverlay(ctx);
    }
    return InputConsumed::Yes;
}

void ClipTool::OnHoverEnd(ToolContext& ctx)
{
    if (HotButton == -1 && HotHandle == -1)
        return;
    HotButton = -1;
    HotHandle = -1;
    if (Phase != ClipPhase::Idle)
        WriteOverlay(ctx);
}

InputConsumed ClipTool::OnClick(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer)
{
    const int button = ButtonUnderCursor(viewport, pointer.Position);
    if (button >= 0)
    {
        const ButtonRow row = BuildButtons();
        switch (row.Roles[static_cast<std::size_t>(button)])
        {
        case ClipButton::Front:   SetMode(ctx, ClipMode::KeepFront); break;
        case ClipButton::Back:    SetMode(ctx, ClipMode::KeepBack); break;
        case ClipButton::Split:   SetMode(ctx, ClipMode::Split); break;
        case ClipButton::Cap:     SetCapped(ctx, !Capped); break;
        case ClipButton::Confirm: if (CanCommit()) Commit(ctx); break;
        case ClipButton::Cancel:  RevertAll(ctx); break;
        }
        return InputConsumed::Yes;
    }
    // A click is not a line; consuming it keeps a stray click from re-selecting
    // while the tool is active, and a pending clip survives.
    return InputConsumed::Yes;
}

bool ClipTool::CaptureTargets(ToolContext& ctx)
{
    Targets.clear();
    for (const SelectableRef& ref : ctx.Selection.GetSelection())
    {
        if (!ref.IsValid())
            continue;
        const bool seen = std::any_of(Targets.begin(), Targets.end(),
                                      [&](const Target& t) { return t.Entity == ref.Entity; });
        if (seen)
            continue;
        const std::optional<MeshEditTargetMesh> resolved = ctx.Sink.ResolveMesh(ref.Entity);
        if (!resolved.has_value() || resolved->Mesh == nullptr)
            continue;
        Target target;
        target.Entity = ref.Entity;
        target.Original = *resolved->Mesh;
        target.Transform = resolved->Transform;
        Targets.push_back(std::move(target));
    }
    return !Targets.empty();
}

std::optional<Vec3d> ClipTool::SnappedPoint(ToolContext& ctx, const EditorViewport& viewport, ImVec2 pos) const
{
    return ctx.Picking.ProjectPointToPlane(viewport, pos, SnapPlane);
}

bool ClipTool::CaptureGesture(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos)
{
    SourceViewport = viewport.Id;
    const bool perspective = viewport.GetOrientationTraits().Mode == EditorCamera::Mode::Perspective;

    // The snap plane: the view's grid, or in perspective the face under the
    // press so the line snaps on the wall it is drawn on. Chosen once; the drag
    // never re-picks, and the cut stands on this plane's normal.
    SnapPlane = viewport.GetGrid(ctx.Grid);
    if (perspective)
        if (const std::optional<SurfaceHit> hit = ctx.Picking.PickSurface(viewport, pos, ctx.Scene))
        {
            Vec3d u, v;
            InPlaneAxes(hit->Normal, u, v);
            SnapPlane = GridFrame::SnapPlaneOnFace(hit->Point, hit->Normal, u, v, ctx.Grid);
        }

    const std::optional<Vec3d> start = SnappedPoint(ctx, viewport, pos);
    if (!start.has_value())
        return false;
    A = *start;
    B = *start;
    Reach = 0.0f;
    return true;
}

std::unique_ptr<IInteraction> ClipTool::BeginDrag(ToolContext& ctx, EditorViewport& viewport,
                                                  const PointerEvent& pressPointer)
{
    if (ButtonUnderCursor(viewport, pressPointer.Position) >= 0)
        return nullptr; // the press belongs to a viewport button

    if (Phase == ClipPhase::Pending)
    {
        const int handle = HandleUnderCursor(viewport, pressPointer.Position);
        if (handle >= 0)
        {
            Phase = ClipPhase::Dragging;
            return std::make_unique<ClipDragInteraction>(*this, handle);
        }
        RevertAll(ctx); // a fresh line replaces the pending one
    }

    if (!CaptureTargets(ctx))
    {
        Status = "select a brush to clip";
        return nullptr;
    }
    if (!CaptureGesture(ctx, viewport, pressPointer.Position))
    {
        Targets.clear();
        return nullptr;
    }
    Phase = ClipPhase::Dragging;
    Committable = false;
    Status.clear();
    // ctx is the workspace-owned ToolContext every tool entry point receives, so
    // it outlives this scope; Commit and RevertAll close it.
    ctx.Commands.OpenPendingEdit([this, &ctx] { RevertAll(ctx); });
    return std::make_unique<ClipDragInteraction>(*this, -1);
}

void ClipTool::UpdateDrag(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos, int endpoint)
{
    if (Phase != ClipPhase::Dragging || viewport.Id != SourceViewport)
        return;
    const std::optional<Vec3d> point = SnappedPoint(ctx, viewport, pos);
    if (!point.has_value())
        return;
    (endpoint == 0 ? A : B) = *point;
    RefreshPreview(ctx);
}

void ClipTool::EndDrag(ToolContext& ctx, EditorViewport& viewport)
{
    if (Phase != ClipPhase::Dragging)
        return;
    // A release never discards the gesture: a line too short to stand a plane
    // on pends with its pins where they are, and dragging one apart is the cut.
    // The construction line's reach: what the source view's diagonal spans at
    // A, captured now and kept, so the line never grows or shrinks with a camera
    // moving afterwards.
    const ViewportProjection projection(viewport);
    const float diagonal = std::hypot(viewport.RegionMax.x - viewport.RegionMin.x,
                                      viewport.RegionMax.y - viewport.RegionMin.y);
    Reach = projection.WorldSizeForPixels(A, diagonal);
    Phase = ClipPhase::Pending;
    WriteOverlay(ctx);
}

void ClipTool::RestorePreviews(ToolContext& ctx)
{
    for (const Target& target : Targets)
        if (ctx.Sink.ResolveMesh(target.Entity).has_value())
            ctx.Sink.PreviewMesh(target.Entity, target.Original);
    ctx.Preview.Clear();
}

void ClipTool::RefreshPreview(ToolContext& ctx)
{
    if (Phase == ClipPhase::Idle)
        return;

    ClipPlane = ClipPlaneMath::ThroughLineAndDirection(A, B, SnapPlane.AxisU.Cross(SnapPlane.AxisV));
    Crossed = Missed = Failed = 0;
    if (!ClipPlane.has_value())
    {
        RestorePreviews(ctx);
        Committable = false;
        Status = "drag a pin to set the cut";
        WriteOverlay(ctx);
        return;
    }

    const Vec3d front = A + ClipPlane->Normal;
    // What the preview buffer carries: the discarded half for a keep, the
    // section the cut makes for a split -- the brush itself stays whole there.
    BrushMesh wire;
    const Vec4 wireColor = Mode == ClipMode::Split ? EditorTheme::Readout : EditorTheme::ContextZoneDim;
    for (Target& target : Targets)
    {
        const Plane local = ClipPlaneMath::InLocal(*ClipPlane, front, target.Transform);
        // Missed is a fact about the brush, not about the kernel's answer: the
        // plane crosses it only if it has vertices strictly on both sides.
        bool anyFront = false, anyBack = false;
        for (const BrushVertex& vertex : target.Original.Vertices)
        {
            const float d = local.SignedDistanceTo(vertex.Position);
            anyFront = anyFront || d > BrushOps::kClipSnap;
            anyBack = anyBack || d < -BrushOps::kClipSnap;
        }
        if (!anyFront || !anyBack)
        {
            target.Result = Outcome::Missed;
            ++Missed;
            ctx.Sink.PreviewMesh(target.Entity, target.Original);
            continue;
        }
        const BrushOps::ClipCap cap = Capped ? BrushOps::ClipCap::Capped : BrushOps::ClipCap::Open;
        target.Front = BrushOps::Clip(target.Original, local, true, cap);
        target.Back = BrushOps::Clip(target.Original, local, false, cap);
        if (target.Front.Faces.empty() || target.Back.Faces.empty())
        {
            // A crossing plane with an empty half is a cut the kernel could
            // not complete, not a miss.
            target.Result = Outcome::Invalid;
            ++Failed;
            ctx.Sink.PreviewMesh(target.Entity, target.Original);
            continue;
        }
        // A capped cut of a closed solid must come out as two closed solids:
        // the one way the clip kernel fails is a cut whose cap is more than
        // one loop (across a tunnel, say), which it cannot close. An open cut
        // is open by request and only has to be a usable mesh.
        BrushMesh originalCheck = target.Original;
        BrushMesh frontCheck = target.Front;
        BrushMesh backCheck = target.Back;
        const bool wasClosed = Capped && BrushValidateAndRepair(originalCheck).Closed;
        const BrushRepairResult frontReport = BrushValidateAndRepair(frontCheck);
        const BrushRepairResult backReport = BrushValidateAndRepair(backCheck);
        const bool sound = frontReport.Ok && backReport.Ok
            && (!wasClosed || (frontReport.Closed && backReport.Closed));
        if (!sound)
        {
            target.Result = Outcome::Invalid;
            ++Failed;
            ctx.Sink.PreviewMesh(target.Entity, target.Original);
            continue;
        }
        target.Result = Outcome::Valid;
        ++Crossed;
        if (Mode == ClipMode::Split)
        {
            // Nothing goes: the brush stays whole and the section shows where
            // the two halves will part, whatever the cap toggle says.
            ctx.Sink.PreviewMesh(target.Entity, target.Original);
            const BrushMesh capped = Capped ? target.Front
                                            : BrushOps::Clip(target.Original, local, true, BrushOps::ClipCap::Capped);
            BrushMesh section;
            for (const BrushFace& face : capped.Faces)
            {
                const Vec3d normal = BrushComputeFaceNormal(capped, face);
                if (std::abs(normal.Dot(local.Normal)) < 0.999f)
                    continue;
                bool onPlane = true; // parallel is not enough: the box's own side may be
                for (std::uint32_t index : face.Loop)
                    onPlane = onPlane && std::abs(local.SignedDistanceTo(capped.Vertices[index].Position)) < 1e-3f;
                if (!onPlane)
                    continue;
                BrushFace copy = face;
                for (std::uint32_t& index : copy.Loop)
                {
                    section.Vertices.push_back(capped.Vertices[index]);
                    index = static_cast<std::uint32_t>(section.Vertices.size() - 1);
                }
                section.Faces.push_back(std::move(copy));
            }
            AppendInWorld(wire, section, target.Transform);
            continue;
        }
        // The entity shows what it keeps; what it loses draws dim.
        const BrushMesh& kept = Mode == ClipMode::KeepBack ? target.Back : target.Front;
        const BrushMesh& other = Mode == ClipMode::KeepBack ? target.Front : target.Back;
        ctx.Sink.PreviewMesh(target.Entity, kept);
        AppendInWorld(wire, other, target.Transform);
    }
    if (wire.Faces.empty())
        ctx.Preview.Clear();
    else
        ctx.Preview.SetMesh(Transform3f::Identity(), std::move(wire), wireColor);

    Committable = Failed == 0 && Crossed > 0;
    if (Failed > 0)
        Status = std::to_string(Failed) + (Failed == 1 ? " selected brush could not be clipped"
                                                       : " selected brushes could not be clipped");
    else if (Crossed == 0)
        Status = "the line misses every selected brush";
    else
        Status.clear();
    WriteOverlay(ctx);
}

void ClipTool::WriteOverlay(ToolContext& ctx)
{
    ctx.Overlay.Segments.clear();
    ctx.Overlay.PointHandles.clear();
    ctx.Overlay.ViewportButtons.clear();
    ctx.Overlay.Readout.Clear();
    if (Phase == ClipPhase::Idle)
        return;

    // The line itself, bright, and the construction line through it, dim and
    // reaching past both ends, in every view.
    const Vec3d direction = (B - A).Magnitude() > 1e-6f ? (B - A).Normalized() : Vec3d{};
    if (Reach > 0.0f && direction.SqrMagnitude() > 0.0f)
    {
        WorldSegmentRequest construction;
        construction.From = A - direction * Reach;
        construction.To = B + direction * Reach;
        construction.Color = EditorTheme::Readout;
        construction.Color.W = 0.35f;
        construction.Thickness = 1.0f;
        ctx.Overlay.Segments.push_back(construction);
    }
    WorldSegmentRequest line;
    line.From = A;
    line.To = B;
    line.Color = EditorTheme::Readout;
    line.Thickness = 2.0f;
    ctx.Overlay.Segments.push_back(line);

    if (Phase == ClipPhase::Pending)
    {
        const Vec3d ends[2] = { A, B };
        for (int i = 0; i < 2; ++i)
        {
            PointHandleRequest handle;
            handle.World = ends[i];
            handle.Fill = HotHandle == i ? EditorTheme::Hover : EditorTheme::Handle;
            handle.Border = EditorTheme::Readout;
            handle.SizePixels = EditorTheme::HandlePixels;
            ctx.Overlay.PointHandles.push_back(handle);
        }
        ViewportButtonRequest request;
        request.Anchors = { A, B };
        ButtonRow row = BuildButtons();
        request.Buttons = std::move(row.Buttons);
        request.Hot = HotButton;
        request.Viewport = SourceViewport;
        ctx.Overlay.ViewportButtons.push_back(std::move(request));
    }

    DragReadout& readout = ctx.Overlay.Readout;
    readout.From = A;
    readout.To = B;
    readout.Text = FormatUnits((B - A).Magnitude());
    readout.Text += "  ";
    readout.Text += ModeText(Mode);
    if (!Status.empty())
        readout.Text += "  " + Status;
    else if (Missed > 0)
        readout.Text += "  " + std::to_string(Crossed) + " crossed, " + std::to_string(Missed) + " missed";
    if (CanCommit())
        readout.Text += "  Enter to apply";
    readout.Viewport = SourceViewport;
}

InputConsumed ClipTool::OnKeyDown(ToolContext& ctx, const KeyDownEvent& event)
{
    if (Phase == ClipPhase::Dragging)
        return InputConsumed::No;
    if (event.Key == SDLK_TAB)
    {
        SetMode(ctx, Mode == ClipMode::KeepFront ? ClipMode::KeepBack
                    : Mode == ClipMode::KeepBack ? ClipMode::Split : ClipMode::KeepFront);
        return InputConsumed::Yes;
    }
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
        if (Phase == ClipPhase::Pending)
        {
            RevertAll(ctx);
            return InputConsumed::Yes;
        }
        // Idle: hand off to select, the way a construction tool bows out.
        if (ctx.ActivateTool)
        {
            ctx.ActivateTool("select");
            return InputConsumed::Yes;
        }
        return InputConsumed::No;
    }
    return InputConsumed::No;
}

void ClipTool::OnDeactivate(ToolContext& ctx) { RevertAll(ctx); }
void ClipTool::OnCancel(ToolContext& ctx) { RevertAll(ctx); }

void ClipTool::CommitPending(ToolContext& ctx)
{
    if (CanCommit())
        Commit(ctx);
    else
        RevertAll(ctx);
}

void ClipTool::Commit(ToolContext& ctx)
{
    if (!CanCommit())
        return;

    // Clear state into locals first: the commit re-enters through the sink and
    // the selection, and a reentrant RevertAll must then no-op.
    std::vector<Target> targets = std::move(Targets);
    const ClipMode mode = Mode;
    Targets.clear();
    ctx.Commands.ClosePendingEdit();
    Phase = ClipPhase::Idle;
    Committable = false;
    ClipPlane.reset();
    ctx.Preview.Clear();
    WriteOverlay(ctx);

    // The live meshes are previews; put the originals back so the commands'
    // "before" is what the document holds.
    for (const Target& target : targets)
        if (ctx.Sink.ResolveMesh(target.Entity).has_value())
            ctx.Sink.PreviewMesh(target.Entity, target.Original);

    // The kernel answers what the half-space intersection is; what a shell
    // is, is decided here: the entity keeps the first, every other shell --
    // of a kept half that came apart, or of a split's two halves -- becomes
    // its own brush. One brush, one solid.
    std::vector<SplitEdit> splits;
    std::vector<MeshEdit> edits;
    std::vector<SelectableRef> kept;
    const RegistryId registry = ctx.Scene.GetRegistry().Id;
    for (Target& target : targets)
    {
        if (target.Result != Outcome::Valid || !ctx.Sink.ResolveMesh(target.Entity).has_value())
            continue;
        std::vector<BrushMesh> shells;
        if (mode == ClipMode::Split || mode == ClipMode::KeepFront)
            for (BrushMesh& shell : BrushConnectedComponents(target.Front))
                shells.push_back(std::move(shell));
        if (mode == ClipMode::Split || mode == ClipMode::KeepBack)
            for (BrushMesh& shell : BrushConnectedComponents(target.Back))
                shells.push_back(std::move(shell));
        if (shells.empty())
            continue;
        BrushMesh keep = std::move(shells.front());
        shells.erase(shells.begin());
        if (shells.empty())
        {
            edits.push_back(MeshEdit{ target.Entity, std::move(target.Original), std::move(keep) });
            kept.push_back(SelectableRef::EntitySelection(registry, target.Entity));
            continue;
        }
        splits.push_back(SplitEdit{ target.Entity, std::move(target.Original), std::move(keep), std::move(shells) });
    }
    if (!splits.empty())
    {
        // Every piece ends up selected: the splits select theirs, and the
        // plain edits are added to that.
        ctx.Sink.CommitSplits(std::move(splits));
        if (!edits.empty())
        {
            ctx.Sink.CommitMeshes(std::move(edits));
            std::vector<SelectableRef> all(ctx.Selection.GetSelection().begin(), ctx.Selection.GetSelection().end());
            all.insert(all.end(), kept.begin(), kept.end());
            ctx.Sink.SelectElements(all);
        }
        return;
    }
    ctx.Sink.CommitMeshes(std::move(edits));
    ctx.Sink.SelectElements(kept);
}

void ClipTool::RevertAll(ToolContext& ctx)
{
    if (Phase != ClipPhase::Idle)
    {
        ctx.Commands.ClosePendingEdit();
        RestorePreviews(ctx);
    }
    Phase = ClipPhase::Idle;
    Committable = false;
    ClipPlane.reset();
    Targets.clear();
    Status.clear();
    HotButton = -1;
    HotHandle = -1;
    WriteOverlay(ctx);
}

void ClipTool::DrawToolbarControls(ToolContext& ctx)
{
    const float size = EditorChrome::BarButtonSize();
    for (const ClipMode mode : { ClipMode::KeepFront, ClipMode::KeepBack, ClipMode::Split })
    {
        const char* id = mode == ClipMode::KeepFront ? "clipfront" : mode == ClipMode::KeepBack ? "clipback" : "clipsplit";
        if (EditorChrome::ToolButton(id, ModeIcon(mode), ModeTooltip(mode), Mode == mode, size))
            SetMode(ctx, mode);
        ImGui::SameLine();
    }
    if (EditorChrome::ToolButton("clipcap", IconId::ClipCap, kCapTooltip, Capped, size))
        SetCapped(ctx, !Capped);
}

void ClipTool::DrawProperties(ToolContext& ctx)
{
    EditorChrome::SectionTitle("Clip");
    DrawToolbarControls(ctx);
    ImGui::TextDisabled("Tab cycles.  Drag a line across the selected brushes.");
    if (Phase == ClipPhase::Pending)
    {
        if (!Status.empty())
            ImGui::TextDisabled("%s", Status.c_str());
        if (EditorChrome::Button("clipapply", "Apply", ImVec2(0, 0),
                                 CanCommit() ? EditorChrome::ButtonTone::Active : EditorChrome::ButtonTone::Normal)
            && CanCommit())
            Commit(ctx);
        ImGui::SameLine();
        if (EditorChrome::Button("clipcancel", "Cancel", ImVec2(0, 0), EditorChrome::ButtonTone::Normal))
            RevertAll(ctx);
    }
}
