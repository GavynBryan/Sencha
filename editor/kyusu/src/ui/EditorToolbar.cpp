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

EditorToolbar::EditorToolbar(ToolRegistry& tools, MeshEditService& meshEdit, GridSettings& grid,
                             BrushCreationSettings& brushCreate, EdgeCutSettings& edgeCut)
    : Tools(tools)
    , MeshEdit(meshEdit)
    , Grid(grid)
    , BrushCreate(brushCreate)
    , EdgeCut(edgeCut)
{
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
        const ITool* activeTool = Tools.GetActiveTool();
        if (activeTool != nullptr && activeTool->GetId() == "select")
        {
            // Element modes only mean something to selection; the brush tool
            // just makes brushes.
            DrawModeGroup(buttonSize);
            Divider(buttonSize);
        }
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
    // Brush create sub-mode (Cube/Plane/Cylinder): drives BrushCreationSettings.
    // Only meaningful while the Brush tool is active, so it shows only then.
    const ITool* activeTool = Tools.GetActiveTool();
    if (activeTool != nullptr && activeTool->GetId() == "brush")
    {
        struct PrimitiveButton { BrushPrimitive Kind; const char* Icon; const char* Label; };
        static constexpr std::array<PrimitiveButton, 3> kPrimitives = {{
            { BrushPrimitive::Box, ICON_FA_CUBE, "Cube" },
            { BrushPrimitive::Plane, ICON_FA_SQUARE, "Plane" },
            { BrushPrimitive::Cylinder, ICON_FA_CIRCLE, "Cylinder" },
        }};
        bool firstPrim = true;
        for (const PrimitiveButton& prim : kPrimitives)
        {
            if (!firstPrim)
                ImGui::SameLine();
            firstPrim = false;
            const bool active = BrushCreate.ActivePrimitive == prim.Kind;
            if (ToolButton(prim.Label, prim.Icon, prim.Label, active, buttonSize))
                BrushCreate.ActivePrimitive = prim.Kind;
        }

        if (BrushCreate.ActivePrimitive == BrushPrimitive::Cylinder)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(72.0f);
            if (ImGui::DragInt("##cylsides", &BrushCreate.CylinderSides, 0.25f, 3, 64, "%d sides"))
                BrushCreate.CylinderSides = std::clamp(BrushCreate.CylinderSides, 3, 64);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Cylinder sides");
        }
        Divider(buttonSize);
    }

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

    // Face carve: Apply/Cancel for the pending rect. Clicking synthesizes the
    // exact key path (ToolRegistry routes KeyDownEvent to the active tool), so
    // there is only ONE commit/cancel code path, and it works when the cursor
    // is over the toolbar where viewport keys never arrive.
    if (activeTool != nullptr && activeTool->GetId() == "facecarve")
    {
        const bool pending = static_cast<const FaceCarveTool*>(activeTool)->HasPending();
        if (!pending)
            ImGui::BeginDisabled();
        if (ToolButton("carveapply", ICON_FA_CHECK, "Apply carve  [Enter]", false, buttonSize))
            Tools.OnInput(InputEvent{ KeyDownEvent{ SDLK_RETURN, {} } });
        ImGui::SameLine();
        if (ToolButton("carvecancel", ICON_FA_XMARK, "Cancel carve  [Esc]", false, buttonSize))
            Tools.OnInput(InputEvent{ KeyDownEvent{ SDLK_ESCAPE, {} } });
        if (!pending)
            ImGui::EndDisabled();
        Divider(buttonSize);
    }
}

void EditorToolbar::DrawModeGroup(float buttonSize)
{
    // Mesh element mode (Object/Vertex/Edge/Face): drives MeshEditService.
    // Order + labels come from the shared MeshElementKind traits; only the
    // icon glyph is a UI-local presentation choice (keyed by kind).
    static constexpr std::array<const char*, MeshElementKindCount> kModeIcons = {
        ICON_FA_CUBE,          // Object
        ICON_FA_CIRCLE_DOT,    // Vertex
        ICON_FA_GRIP_LINES,    // Edge
        ICON_FA_VECTOR_SQUARE, // Face
    };
    bool firstMode = true;
    for (MeshElementKind kind : AllMeshElementKinds())
    {
        if (!firstMode)
            ImGui::SameLine();
        firstMode = false;
        const char* label = Traits(kind).Label;
        const bool active = MeshEdit.GetElementKind() == kind;
        if (ToolButton(label, kModeIcons[static_cast<std::size_t>(kind)], label, active, buttonSize))
            MeshEdit.SetElementKind(kind);
    }
}

void EditorToolbar::DrawTransformGroup(float buttonSize)
{
    if (Transform.Session == nullptr)
        return;
    ManipulatorSession& session = *Transform.Session;

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
        if (!hasPivot)
            ImGui::BeginDisabled();
        if (ToolButton("setorigin", ICON_FA_ANCHOR, "Set origin to pivot", false, buttonSize)
            && Transform.SetOriginToPivot)
            Transform.SetOriginToPivot();
        if (!hasPivot)
            ImGui::EndDisabled();
    }
}

void EditorToolbar::DrawGridGroup(float buttonSize)
{
    // Grid snap toggle + spacing: drives the shared GridSettings, so picking,
    // manipulators and brush-create all honor it.
    if (ToolButton("snap", ICON_FA_MAGNET,
                   Grid.SnapEnabled ? "Grid snap: on" : "Grid snap: off",
                   Grid.SnapEnabled, buttonSize))
        Grid.SnapEnabled = !Grid.SnapEnabled;

    ImGui::SameLine();
    ImGui::SetNextItemWidth(96.0f);
    static const float kGridSizes[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f };
    char preview[32];
    std::snprintf(preview, sizeof(preview), ICON_FA_BORDER_ALL "  %g", Grid.Spacing);
    if (ImGui::BeginCombo("##gridsize", preview))
    {
        for (float size : kGridSizes)
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
