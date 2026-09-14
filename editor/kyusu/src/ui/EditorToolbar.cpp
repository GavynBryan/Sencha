#include "EditorToolbar.h"

#include "ToolbarRowPlacement.h"
#include "editmodes/TransformModeItems.h"

#include "ui/EditorUiStyle.h"
#include "ui/chrome/ChromeBars.h"
#include "ui/chrome/ChromeControls.h"
#include "fonts/IconsFontAwesome6.h"

#include "editmodes/ManipulatorSession.h"
#include "meshedit/MeshElementKind.h"
#include "meshedit/MeshElementKindTraits.h"
#include "meshedit/MeshEditService.h"
#include "tools/ITool.h"
#include "tools/ToolRegistry.h"
#include "viewport/GridSettings.h"
#include "viewport/WorldViewSettings.h"

#include <algorithm>
#include <array>

#include <imgui.h>

#include <string>

EditorToolbar::EditorToolbar(std::function<ToolRegistry*()> tools,
                             std::function<ManipulatorSession*()> session,
                             MeshEditService& meshEdit, GridSettings& grid,
                             WorldViewSettings& worldView)
    : ToolsResolver(std::move(tools))
    , SessionResolver(std::move(session))
    , MeshEdit(meshEdit)
    , Grid(grid)
    , WorldView(worldView)
{
}

ToolRegistry& EditorToolbar::Tools() const
{
    return *ToolsResolver();
}

ManipulatorSession* EditorToolbar::Session() const
{
    return SessionResolver();
}

float EditorToolbar::ViewportRowHeight()
{
    return EditorChrome::BarHeight(EditorChrome::BarButtonSize());
}

float EditorToolbar::GizmoStripWidth(float buttonSize)
{
    return buttonSize * 4.0f + ImGui::GetStyle().ItemSpacing.x * 3.0f;
}

void EditorToolbar::DrawViewportRow(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float centerX)
{
    const float buttonSize = EditorChrome::BarButtonSize();
    const EditorChrome::BarRects bar = EditorChrome::BarFrame(
        dl, mn, mx, EditorChrome::BarEdge::Bottom, buttonSize, Surface ? Surface() : EditorChrome::BarSurface{});
    const float moduleGap = EditorUi::Px(EditorUi::Metrics.ModulePad * 3.0f);
    const float edgePad = EditorUi::Px(EditorUi::Metrics.ModulePad * 2.0f);
    const float channelMin = bar.ChannelMin.x + edgePad;
    const float channelMax = bar.ChannelMax.x - edgePad;

    // A block's width is the rect of a group around it, taken after the
    // group; a module that submitted nothing leaves the cursor where it was
    // and counts as no block, since an empty group's rect is not reliably empty.
    const auto measure = [](const auto& draw)
    {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::BeginGroup();
        draw();
        const ImVec2 end = ImGui::GetCursorScreenPos();
        const bool drew = end.x != origin.x || end.y != origin.y;
        ImGui::EndGroup();
        return drew ? ImGui::GetItemRectSize().x : 0.0f;
    };

    // The tool context sits at the channel's left end whatever its width, so
    // it is drawn first and measured in this frame: the tool changing never
    // leaves the strip a frame off its centre. The tail and the grid are
    // placed by their widest so far; the first frame of a session, with no
    // measurement at all, takes the narrow row once.
    ImGui::SetCursorScreenPos(ImVec2(channelMin, bar.LaneMin.y));
    const float leftWidth = measure([&] { DrawToolContextGroup(); });

    const ToolbarRow::Widths widths{
        .Left = leftWidth,
        .Strip = GizmoStripWidth(buttonSize),
        .Tail = TailWidth,
        .Right = GridWidth,
    };
    ToolbarRow::Placement placed = ToolbarRow::Place(channelMin, channelMax, widths, moduleGap, centerX);
    if (!RowMeasured)
        placed.Centered = false;

    // The transform module: the tail (gizmo space, pivot pair) then the
    // strip, so the strip's own edge is what the placement centred.
    ImGui::SetCursorScreenPos(ImVec2(placed.Center - (placed.Centered ? widths.Tail : 0.0f), bar.LaneMin.y));
    const float transformWidth = measure([&]
    {
        EditorChrome::ModuleScope module("transform");
        DrawTransformTail(buttonSize);
        DrawGizmoStrip(buttonSize);
    });
    TailWidth = std::max(TailWidth, transformWidth - widths.Strip);

    // In the narrow row the grid follows the transform module rather than
    // its own anchor, so the first frame's unknown width still lands it in order.
    const float rightX = placed.Centered ? placed.Right : ImGui::GetItemRectMax().x + moduleGap;
    ImGui::SetCursorScreenPos(ImVec2(rightX, bar.LaneMin.y));
    const float gridWidth = measure([&]
    {
        EditorChrome::ModuleScope module("grid");
        DrawGridGroup(buttonSize);
    });
    GridWidth = std::max(GridWidth, gridWidth);
    RowMeasured = true;

    if (placed.Centered)
    {
        // Technical markings in the runs the row left free.
        EditorChrome::BarMarkings(dl, bar, placed.Left + leftWidth + moduleGap, placed.Center - widths.Tail - moduleGap);
        EditorChrome::BarMarkings(dl, bar, placed.Center + widths.Strip + moduleGap, placed.Right - moduleGap);
    }
}

void EditorToolbar::DrawToolContextGroup()
{
    // Whatever the active tool wants here, it draws itself: a tool with contextual
    // controls ships them with its own code rather than as a branch in the bar.
    ToolRegistry& tools = Tools();
    ITool* active = tools.GetActiveTool();
    if (active == nullptr)
        return;

    // The row places the blocks after it; a tool that drew nothing leaves
    // the cursor untouched, which is how the row knows there is no block.
    EditorChrome::ModuleScope module("toolcontext");
    active->DrawToolbarControls(tools.GetContext());
}

void EditorToolbar::DrawGizmoStrip(float buttonSize)
{
    if (Session() == nullptr)
        return;
    ManipulatorSession& session = *Session();

    // The gizmo (Shift+Q/W/E/R), from the one mode table the gizmo wheel also
    // reads. Highlights the EFFECTIVE mode: with Resize chosen but nothing
    // resizable selected, Move is what the user is driving.
    const TransformMode effective = session.EffectiveMode();
    bool first = true;
    for (const TransformModeItem& item : kTransformModeItems)
    {
        if (!first)
            ImGui::SameLine();
        first = false;
        if (EditorChrome::ToolButton(item.Tooltip, item.Choice.Icon, item.Tooltip, effective == item.Mode, buttonSize))
            session.SetTransformMode(item.Mode);
    }
}

void EditorToolbar::DrawTransformTail(float buttonSize)
{
    if (Session() == nullptr)
        return;
    ManipulatorSession& session = *Session();

    // Gizmo frame (Shift+T cycles): grid follows the workspace grid frame,
    // local follows the primary selection's rotation.
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.0f);
    const TransformSpace space = session.GetTransformSpace();
    if (EditorChrome::BeginCombo("##gizmospace", TransformSpaceLabel(space)))
    {
        for (TransformSpace candidate : { TransformSpace::Grid, TransformSpace::World, TransformSpace::Local })
            if (ImGui::Selectable(TransformSpaceLabel(candidate), candidate == space))
                session.SetTransformSpace(candidate);
        EditorChrome::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Gizmo axes: grid frame, world, or the selection's local axes  [Shift+T]");

    // Pivot pair, shown only while something is selected: Edit Pivot retargets
    // the Move gizmo to the transient pivot; Set Origin commits the moved pivot
    // into the primary brush's origin.
    if (Transform.HasSelection && Transform.HasSelection())
    {
        ImGui::SameLine();
        const bool editingPivot = session.IsEditingPivot();
        if (EditorChrome::ToolButton("editpivot", IconId::Pivot,
                       editingPivot ? "Pivot: editing (Move gizmo drags it)" : "Edit pivot",
                       editingPivot, buttonSize))
            session.SetEditingPivot(!editingPivot);

        ImGui::SameLine();
        const bool hasPivot = session.HasPivotOverride();
        if (EditorChrome::ToolButton("setorigin", IconId::Anchor, "Set origin...", false, buttonSize))
            ImGui::OpenPopup("##setorigin_menu");
        if (ImGui::BeginPopup("##setorigin_menu"))
        {
            if (!hasPivot)
                ImGui::BeginDisabled();
            if (ImGui::MenuItem("To pivot") && Transform.SetOriginToPivot)
                Transform.SetOriginToPivot();
            if (!hasPivot)
                ImGui::EndDisabled();
            if (ImGui::MenuItem("To selected vertex") && Transform.SetOriginToVertex)
                Transform.SetOriginToVertex();
            if (ImGui::MenuItem("To bounds center") && Transform.SetOriginToBoundsCenter)
                Transform.SetOriginToBoundsCenter();
            if (ImGui::MenuItem("To bounds corner (min X,Y,Z)") && Transform.SetOriginToBoundsCorner)
                Transform.SetOriginToBoundsCorner();
            ImGui::EndPopup();
        }
    }
    // The strip follows on the lane.
    ImGui::SameLine();
}

void EditorToolbar::DrawGridGroup(float buttonSize)
{
    // Grid snap toggle + target + spacing: drives the shared GridSettings, so
    // picking, manipulators and brush-create all honor it.
    if (EditorChrome::ToolButton("snap", IconId::Snap,
                   Grid.SnapEnabled ? "Snap: on" : "Snap: off",
                   Grid.SnapEnabled, buttonSize))
        Grid.SnapEnabled = !Grid.SnapEnabled;

    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.0f);
    static constexpr const char* kSnapTargetLabels[] = { "Grid", "Vertex", "Edge", "Face" };
    const int targetIndex = static_cast<int>(Grid.Target);
    if (EditorChrome::BeginCombo("##snaptarget", kSnapTargetLabels[targetIndex]))
    {
        for (int i = 0; i < 4; ++i)
            if (ImGui::Selectable(kSnapTargetLabels[i], i == targetIndex))
                Grid.Target = static_cast<SnapTarget>(i);
        EditorChrome::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Snap target: grid lines, or the vertex/edge/face under the cursor");

    ImGui::SameLine();
    if (EditorChrome::ToolButton("zonebounds", IconId::ZoneBounds,
                   WorldView.ShowZoneBounds ? "Zone bounds: on" : "Zone bounds: off",
                   WorldView.ShowZoneBounds, buttonSize))
        WorldView.ShowZoneBounds = !WorldView.ShowZoneBounds;

#ifdef SENCHA_ENABLE_RENDER_PROFILING
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.8f);
    if (EditorChrome::BeginCombo("##renderdebugview",
                          RenderDebugViewLabel(WorldView.DebugViewMode)))
    {
        for (std::uint32_t index = 0; index < kRenderDebugViewCount; ++index)
        {
            const RenderDebugView candidate = static_cast<RenderDebugView>(index);
            const bool selected = candidate == WorldView.DebugViewMode;
            if (ImGui::Selectable(RenderDebugViewLabel(candidate), selected))
                WorldView.DebugViewMode = candidate;
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        EditorChrome::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Development render channel for Solid viewports");
#endif

    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.4f);
    char preview[32];
    std::snprintf(preview, sizeof(preview), ICON_FA_BORDER_ALL "  %g", Grid.Spacing);
    if (EditorChrome::BeginCombo("##gridsize", preview))
    {
        for (float size : GridSettings::kSpacingSteps)
        {
            char item[16];
            std::snprintf(item, sizeof(item), "%g", size);
            if (ImGui::Selectable(item, Grid.Spacing == size))
                Grid.Spacing = size;
        }
        EditorChrome::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Grid size");

    // Grid frame: move/rotate the working grid to geometry, and reset it.
    // Lit while a custom frame is active so an off-axis grid is never a
    // surprise.
    ImGui::SameLine();
    const bool customFrame = Grid.HasCustomFrame();
    if (EditorChrome::ToolButton("gridframe", IconId::GridFrame,
                                 customFrame ? "Grid frame: custom" : "Grid frame: world", customFrame, buttonSize))
        ImGui::OpenPopup("##gridframe_menu");
    if (ImGui::BeginPopup("##gridframe_menu"))
    {
        const bool movingOrigin = GridFrame.IsMovingOrigin && GridFrame.IsMovingOrigin();
        if (ImGui::MenuItem("Move origin (gizmo)", nullptr, movingOrigin, bool(GridFrame.ToggleMoveOrigin)))
            GridFrame.ToggleMoveOrigin();
        if (ImGui::MenuItem("Origin to selection", "Shift+G", false, bool(GridFrame.OriginToSelection)))
            GridFrame.OriginToSelection();
        if (ImGui::MenuItem("Align to face", "Alt+G", false, bool(GridFrame.AlignToFace)))
            GridFrame.AlignToFace();
        if (ImGui::MenuItem("Rotate 90\xc2\xb0 in plane", nullptr, false, bool(GridFrame.RotateInPlane)))
            GridFrame.RotateInPlane();
        ImGui::Separator();
        if (ImGui::MenuItem("Reset to world", "Ctrl+Shift+G", false, bool(GridFrame.Reset) && customFrame))
            GridFrame.Reset();
        ImGui::EndPopup();
    }
}
