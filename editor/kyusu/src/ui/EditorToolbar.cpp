#include "EditorToolbar.h"

#include "ui/EditorUiSkin.h"
#include "ui/EditorUiStyle.h"
#include "fonts/IconsFontAwesome6.h"

#include "editmodes/ManipulatorSession.h"
#include "meshedit/MeshElementKind.h"
#include "meshedit/MeshElementKindTraits.h"
#include "meshedit/MeshEditService.h"
#include "document/BrushCreationSettings.h"
#include "document/EdgeCutSettings.h"
#include "document/tools/FaceCarveTool.h"
#include "tools/ITool.h"
#include "tools/ToolRegistry.h"
#include "viewport/GridSettings.h"
#include "viewport/WorldViewSettings.h"

#include <algorithm>
#include <array>

#include <imgui.h>
#include <imgui_internal.h> // BeginViewportSideBar (reserves work-area space)

#include <cstdio>
#include <string>

namespace
{
// A square glossy beveled icon button (EditorUiSkin), accent-lit when active.
// `id` is a stable ImGui id; `tooltip` may be dynamic. Returns true when clicked.
bool ToolButton(const char* id, const char* icon, const char* tooltip, bool active, float size)
{
    const bool clicked = EditorUiSkin::Button(id, icon, ImVec2(size, size), active);
    if (tooltip != nullptr && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);
    return clicked;
}

// Vertical divider matching the button height, for grouping the toolbar sections.
void Divider(float height)
{
    ImGui::SameLine();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(p.x + 3.0f, p.y), ImVec2(p.x + 3.0f, p.y + height),
        ImGui::GetColorU32(EditorUiSkin::WithAlpha(EditorUi::Border, 0.9f)));
    ImGui::Dummy(ImVec2(7.0f, height));
    ImGui::SameLine();
}
}

EditorToolbar::EditorToolbar(std::function<ToolRegistry*()> tools,
                             std::function<ManipulatorSession*()> session,
                             MeshEditService& meshEdit, GridSettings& grid,
                             WorldViewSettings& worldView,
                             EdgeCutSettings& edgeCut)
    : ToolsResolver(std::move(tools))
    , SessionResolver(std::move(session))
    , MeshEdit(meshEdit)
    , Grid(grid)
    , WorldView(worldView)
    , EdgeCut(edgeCut)
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

void EditorToolbar::Draw()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float buttonSize = ImGui::GetFrameHeight();
    const float barHeight = buttonSize + style.WindowPadding.y * 2.0f;

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::BeginViewportSideBar("##EditorToolbar", viewport, ImGuiDir_Up, barHeight, flags))
    {
        // Glossy metal band behind the buttons.
        EditorUiSkin::Band(ImGui::GetWindowDrawList(), ImGui::GetWindowPos(),
                           ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                                  ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
                           EditorUi::HeaderBg);

        // Tools themselves live on the left sidebar; the toolbar hosts the
        // active tool's contextual controls and the shared editing groups.
        DrawToolContextGroup(buttonSize);
        DrawTransformGroup(buttonSize);
        Divider(buttonSize);
        DrawGridGroup(buttonSize);
        Divider(buttonSize);
        DrawPlayGroup(buttonSize);
    }
    ImGui::End();
}

void EditorToolbar::DrawToolContextGroup(float buttonSize)
{
    const ITool* activeTool = Tools().GetActiveTool();

    // Edge-cut mode (Loop ring vs Single edge): drives EdgeCutSettings. Shows
    // only while the cut tool is active; Tab toggles it too.
    if (activeTool != nullptr && activeTool->GetId() == "edgecut")
    {
        if (ToolButton("cutloop", ICON_FA_ROTATE, "Loop cut (whole ring)  [Tab]",
                       EdgeCut.LoopCut, buttonSize))
            EdgeCut.LoopCut = true;
        ImGui::SameLine();
        if (ToolButton("cutsingle", ICON_FA_GRIP_LINES, "Single edge cut  [Tab]",
                       !EdgeCut.LoopCut, buttonSize))
            EdgeCut.LoopCut = false;
        Divider(buttonSize);
    }

    // Face carve: Apply/Cancel for the pending draft. Clicking synthesizes the
    // exact key path (ToolRegistry routes KeyDownEvent to the active tool), so
    // there is only ONE commit/cancel code path, and it works when the cursor
    // is over the toolbar where viewport keys never arrive.
    if (activeTool != nullptr && activeTool->GetId() == "facecarve")
    {
        const auto* carve = static_cast<const FaceCarveTool*>(activeTool);
        const bool pending = carve->HasPending();
        const bool canCommit = carve->CanCommit();
        if (!canCommit)
            ImGui::BeginDisabled();
        if (ToolButton("carveapply", ICON_FA_CHECK, "Apply carve  [Enter]", false, buttonSize))
            Tools().OnInput(InputEvent{ KeyDownEvent{ SDLK_RETURN, {} } });
        ImGui::SameLine();
        if (!canCommit)
            ImGui::EndDisabled();
        if (!pending)
            ImGui::BeginDisabled();
        if (ToolButton("carvecancel", ICON_FA_XMARK, "Cancel carve  [Esc]", false, buttonSize))
            Tools().OnInput(InputEvent{ KeyDownEvent{ SDLK_ESCAPE, {} } });
        if (!pending)
            ImGui::EndDisabled();
        Divider(buttonSize);
    }
}

void EditorToolbar::DrawTransformGroup(float buttonSize)
{
    if (Session() == nullptr)
        return;
    ManipulatorSession& session = *Session();

    // The gizmo (Shift+Q/W/E/R). Highlights the EFFECTIVE mode: with Resize
    // chosen but nothing resizable selected, Move is what the user is driving.
    struct GizmoButton { TransformMode Mode; const char* Icon; const char* Tooltip; };
    static constexpr std::array<GizmoButton, 4> kGizmos = {{
        { TransformMode::Resize, ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, "Resize bounds  [Shift+Q]" },
        { TransformMode::Move, ICON_FA_UP_DOWN_LEFT_RIGHT, "Move  [Shift+W]" },
        { TransformMode::Rotate, ICON_FA_ROTATE, "Rotate  [Shift+E]" },
        { TransformMode::Scale, ICON_FA_MAXIMIZE, "Scale  [Shift+R]" },
    }};
    const TransformMode effective = session.EffectiveMode();
    bool first = true;
    for (const GizmoButton& gizmo : kGizmos)
    {
        if (!first)
            ImGui::SameLine();
        first = false;
        if (ToolButton(gizmo.Tooltip, gizmo.Icon, gizmo.Tooltip, effective == gizmo.Mode, buttonSize))
            session.SetTransformMode(gizmo.Mode);
    }

    // Gizmo frame (Shift+T cycles): grid follows the workspace grid frame,
    // local follows the primary selection's rotation.
    ImGui::SameLine();
    ImGui::SetNextItemWidth(76.0f);
    const TransformSpace space = session.GetTransformSpace();
    if (ImGui::BeginCombo("##gizmospace", TransformSpaceLabel(space)))
    {
        for (TransformSpace candidate : { TransformSpace::Grid, TransformSpace::World, TransformSpace::Local })
            if (ImGui::Selectable(TransformSpaceLabel(candidate), candidate == space))
                session.SetTransformSpace(candidate);
        ImGui::EndCombo();
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
        if (ToolButton("editpivot", ICON_FA_CROSSHAIRS,
                       editingPivot ? "Pivot: editing (Move gizmo drags it)" : "Edit pivot",
                       editingPivot, buttonSize))
            session.SetEditingPivot(!editingPivot);

        ImGui::SameLine();
        const bool hasPivot = session.HasPivotOverride();
        if (ToolButton("setorigin", ICON_FA_ANCHOR, "Set origin...", false, buttonSize))
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
}

void EditorToolbar::DrawGridGroup(float buttonSize)
{
    // Grid snap toggle + target + spacing: drives the shared GridSettings, so
    // picking, manipulators and brush-create all honor it.
    if (ToolButton("snap", ICON_FA_MAGNET,
                   Grid.SnapEnabled ? "Snap: on" : "Snap: off",
                   Grid.SnapEnabled, buttonSize))
        Grid.SnapEnabled = !Grid.SnapEnabled;

    ImGui::SameLine();
    ImGui::SetNextItemWidth(76.0f);
    static constexpr const char* kSnapTargetLabels[] = { "Grid", "Vertex", "Edge", "Face" };
    const int targetIndex = static_cast<int>(Grid.Target);
    if (ImGui::BeginCombo("##snaptarget", kSnapTargetLabels[targetIndex]))
    {
        for (int i = 0; i < 4; ++i)
            if (ImGui::Selectable(kSnapTargetLabels[i], i == targetIndex))
                Grid.Target = static_cast<SnapTarget>(i);
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Snap target: grid lines, or the vertex/edge/face under the cursor");

    ImGui::SameLine();
    if (ToolButton("zonebounds", ICON_FA_VECTOR_SQUARE,
                   WorldView.ShowZoneBounds ? "Zone bounds: on" : "Zone bounds: off",
                   WorldView.ShowZoneBounds, buttonSize))
        WorldView.ShowZoneBounds = !WorldView.ShowZoneBounds;

    ImGui::SameLine();
    ImGui::SetNextItemWidth(96.0f);
    char preview[32];
    std::snprintf(preview, sizeof(preview), ICON_FA_BORDER_ALL "  %g", Grid.Spacing);
    if (ImGui::BeginCombo("##gridsize", preview))
    {
        for (float size : GridSettings::kSpacingSteps)
        {
            char item[16];
            std::snprintf(item, sizeof(item), "%g", size);
            if (ImGui::Selectable(item, Grid.Spacing == size))
                Grid.Spacing = size;
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Grid size");

    // Grid frame: move/rotate the working grid to geometry, and reset it.
    // Highlighted while a custom frame is active so an off-axis grid is
    // never a surprise.
    ImGui::SameLine();
    const bool customFrame = Grid.HasCustomFrame();
    if (customFrame)
        ImGui::PushStyleColor(ImGuiCol_Button, EditorUi::AccentDim);
    const bool frameMenu = ImGui::Button(ICON_FA_RULER_COMBINED "##gridframe");
    if (customFrame)
        ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(customFrame ? "Grid frame: custom" : "Grid frame: world");
    if (frameMenu)
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

void EditorToolbar::DrawPlayGroup(float buttonSize)
{
    // Author -> cook -> play loop. Cook bakes the live level; Play launches it
    // out-of-process (PIE); Stop ends the session. Play flips to a lit state
    // while a session runs.
    const bool playing = Play.IsPlaying && Play.IsPlaying();

    if (Play.Cook)
    {
        if (ToolButton("cook", ICON_FA_HAMMER, "Cook level", false, buttonSize))
            Play.Cook();
    }
    ImGui::SameLine();
    if (Play.Play)
    {
        if (ToolButton("play", ICON_FA_PLAY, playing ? "Playing" : "Play (PIE)", playing, buttonSize))
            Play.Play();
    }
    ImGui::SameLine();
    if (Play.Stop)
    {
        if (ToolButton("stop", ICON_FA_STOP, "Stop", false, buttonSize) && playing)
            Play.Stop();
    }
}
