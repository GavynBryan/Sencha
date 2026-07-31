#include "ToolPropertiesPanel.h"

#include "ui/ButtonFlow.h"

#include "ui/EditorUiSkin.h"
#include "ui/EditorUiStyle.h"
#include "ui/ScopedPanel.h"

#include "fonts/IconsFontAwesome6.h"

#include "commands/CommandStack.h"
#include "document/WorldDocument.h"
#include "input/InputEvent.h"
#include "meshedit/IMeshEditTarget.h"
#include "meshedit/ManipulationSink.h"
#include "meshedit/MeshEditService.h"
#include "meshedit/MeshElementKindTraits.h"
#include "project/MaterialLibrary.h"
#include "selection/SelectionService.h"
#include "meshedit/ActiveMaterialState.h"
#include "tools/ToolRegistry.h"
#include "tools/ToolContext.h"
#include "viewport/GridSettings.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <memory>

namespace
{
// Lays buttons left to right, wrapping to a new row when the next one would
// overflow the panel, so a thin panel never clips verbs off its right edge.
}

ToolPropertiesPanel::ToolPropertiesPanel(std::function<IMeshEditTarget*()> target,
                                         std::function<ManipulationSink*()> sink,
                                         std::function<ToolRegistry*()> tools,
                                         SelectionService& selection,
                                         MeshEditService& meshEdit,
                                         CommandStack& commands,
                                         WorldDocument& world,
                                         ActiveMaterialState& activeMaterial,
                                         std::optional<FaceMaterialClipboard>& uvClipboard,
                                         SelectionActions& actions,
                                         PendingBridgeEdit& bridgeEdit,
                                         PendingElementEdit& elementEdit,
                                         std::function<void()> exportGlb)
    : TargetResolver(std::move(target))
    , SinkResolver(std::move(sink))
    , ToolsResolver(std::move(tools))
    , Selection(selection)
    , MeshEdit(meshEdit)
    , Commands(commands)
    , World(world)
    , ActiveMaterial(activeMaterial)
    , UvClipboard(uvClipboard)
    , Actions(actions)
    , BridgeEdit(bridgeEdit)
    , ElementEdit(elementEdit)
    , ExportGlb(std::move(exportGlb))
{
}

IMeshEditTarget& ToolPropertiesPanel::Target() const
{
    return *TargetResolver();
}

ToolRegistry* ToolPropertiesPanel::Tools() const
{
    return ToolsResolver ? ToolsResolver() : nullptr;
}

void ToolPropertiesPanel::BeginInset(float distance)
{
    if (ManipulationSink* sink = SinkResolver ? SinkResolver() : nullptr)
        ElementEdit.BeginInset(*sink, Commands, Selection.GetSelection(), distance);
}

void ToolPropertiesPanel::BeginBevel(float width, int segments)
{
    if (ManipulationSink* sink = SinkResolver ? SinkResolver() : nullptr)
        ElementEdit.BeginBevel(*sink, Commands, Selection.GetSelection(), width, segments);
}

void ToolPropertiesPanel::CommitElementEdit() { ElementEdit.Commit(Commands); }
void ToolPropertiesPanel::CancelElementEdit() { ElementEdit.Cancel(Commands); }
void ToolPropertiesPanel::CommitBridge() { BridgeEdit.Commit(Commands, Selection); }
void ToolPropertiesPanel::CancelBridge() { BridgeEdit.Cancel(Commands); }

std::string_view ToolPropertiesPanel::GetTitle() const
{
    return "Tool Properties";
}

void ToolPropertiesPanel::DrawObjectVerbs()
{
    int selectedBrushes = 0;
    for (const SelectableRef& ref : Selection.GetSelection())
        if (ref.IsEntity() && Target().Resolve(ref.Entity).has_value())
            ++selectedBrushes;
    const bool hasBaked = Actions.HasBakedSelection();

    if (selectedBrushes == 0 && !hasBaked)
    {
        ImGui::TextDisabled("Select a brush to edit it");
        return;
    }

    if (selectedBrushes > 0)
    {
        ButtonFlow flow;
        if (flow.Button("Recalculate Normals"))
        {
            if (auto command = MeshEdit.ApplyVerb(Target(), Selection.GetSnapshot(),
                                                  MeshEditVerb::RecalculateNormals, {}))
                Commands.Execute(std::move(command));
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Re-orients every face outward (concave-safe)");

        if (flow.Button("Duplicate"))
            Actions.Duplicate(false);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Independent copies  [Ctrl+D]");
        if (flow.Button("Make Instance"))
            Actions.Duplicate(true);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Copies sharing this brush's mesh: editing any instance\n"
                              "edits them all, and baking shares one mesh asset.  [Alt+D]");

        if (Actions.HasInstancedSelection())
        {
            if (flow.Button("Make Unique"))
                Actions.MakeUnique();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Break from the instance group, keeping all instanced edits.");
        }

        if (selectedBrushes >= 2 && flow.Button("Merge"))
            Actions.Merge();
        if (selectedBrushes >= 2 && ImGui::IsItemHovered())
            ImGui::SetTooltip("Join the selected brushes into the primary one\n"
                              "(textures keep their placement; no volume boolean).");

        if (flow.Button("Bake to Static Mesh"))
            Actions.Bake();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Writes a .smesh asset and swaps the brush for a placed mesh.\n"
                              "Duplicates of a baked entity share the asset (instances).\n"
                              "Reversible: the source brush stays in the level file.");

        if (hasBaked)
        {
            if (flow.Button("Revert to Brush"))
                Actions.RevertBaked();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Restores the baked entity's editable source brush.");
        }

        if (flow.Button("Export .glb..."))
            ExportGlb();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Exports the selection's baked geometry as binary glTF.");
        return;
    }

    ButtonFlow flow;
    if (hasBaked)
    {
        if (flow.Button("Revert to Brush"))
            Actions.RevertBaked();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Restores the baked entity's editable source brush.");
    }

    if (flow.Button("Export .glb..."))
        ExportGlb();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Exports the selection's baked geometry as binary glTF.");
}

void ToolPropertiesPanel::DrawFaceVerbs()
{
    int selectedFaces = 0;
    for (const SelectableRef& ref : Selection.GetSelection())
    {
        if (ref.IsFace())
            ++selectedFaces;
    }

    ImGui::Text("%d face(s) selected", selectedFaces);
    if (selectedFaces == 0)
    {
        ImGui::TextDisabled("Click a face to select");
        return;
    }

    const auto applyVerb = [&](MeshEditVerb verb, const MeshEditParams& params)
    {
        if (auto command = MeshEdit.ApplyVerb(Target(), Selection.GetSnapshot(), verb, params))
        {
            Commands.Execute(std::move(command));
            // The verb rebuilt the brush: element indices have shifted/reindexed,
            // so any kept element selection would point at the wrong element.
            Selection.ClearMeshElementSelections();
        }
    };

    ButtonFlow flow;
    if (flow.Button("Delete"))
        applyVerb(MeshEditVerb::Delete, {});
    if (flow.Button("Flip Normals"))
        applyVerb(MeshEditVerb::FlipFaceNormal, {});
    if (flow.Button("Separate"))
        Actions.SeparateFaces();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Split these faces into a new brush (the source opens where they were).");

    // Inset: extrude the selected faces along their own normals as one shell.
    // Collapsed to one button until clicked; the pending preview expands into
    // its options (distance regenerates live) until Apply/Cancel.
    const bool pendingInset = ElementEdit.HasPendingInset();
    if (!pendingInset)
    {
        if (flow.Button("Inset...") )
            BeginInset(InsetDistance);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Extrude the selected faces along their own normals\n"
                              "(a rim outward, a recess inward); adjustable until applied.");
    }
    else
    {
        ImGui::SeparatorText("Inset");
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragFloat("Distance", &InsetDistance, 0.05f) )
            ElementEdit.SetInsetDistance(InsetDistance);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Along each face's normal: positive out (a rim), negative in (a recess)");
        if (ImGui::Button("Apply##inset") )
            CommitElementEdit();
        ImGui::SameLine();
        if (ImGui::Button("Cancel##inset") )
            CancelElementEdit();
    }

    DrawTextureTools();

    ImGui::SeparatorText("Modify Texture");
    Uv.Draw(Target(), Selection, Commands, UvClipboard);
}

void ToolPropertiesPanel::DrawTextureTools()
{
    ImGui::SeparatorText("Texture Tools");

    if (ActiveMaterial.Active.IsValid())
        ImGui::TextUnformatted(MaterialDisplayName(ActiveMaterial.Active.Path).c_str());
    else
        ImGui::TextDisabled("No active material (pick one in Materials,\nor Shift+RClick a face).");

    ButtonFlow flow;
    if (!ActiveMaterial.Active.IsValid())
        ImGui::BeginDisabled();
    if (flow.Button("Apply##texture"))
        ApplyMaterialToSelectedFaces(Target(), Selection, Commands, ActiveMaterial.Active);
    if (!ActiveMaterial.Active.IsValid())
        ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Apply the active material to the selected faces.  [Shift+T]");

    if (flow.Button("Clear"))
        ApplyMaterialToSelectedFaces(Target(), Selection, Commands, AssetRef{});
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Drop the faces' own material; they inherit the level default.");

    ImGui::TextDisabled("Level default: %s", World.FocusDocument().GetDefaultMaterial().Path.c_str());
}

void ToolPropertiesPanel::DrawEdgeVerbs()
{
    int selectedEdges = 0;
    for (const SelectableRef& ref : Selection.GetSelection())
    {
        if (ref.IsEdge())
            ++selectedEdges;
    }

    ImGui::Text("%d edge(s) selected", selectedEdges);
    if (selectedEdges == 0)
    {
        ImGui::TextDisabled("Click an edge to select");
        return;
    }

    ButtonFlow flow;
    if (flow.Button("Soften"))
    {
        MeshEditParams params;
        params.Soften = true;
        if (auto command = MeshEdit.ApplyVerb(Target(), Selection.GetSnapshot(), MeshEditVerb::SetEdgeSoftness, params))
            Commands.Execute(std::move(command));
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Shade smooth across the selected edges (green wireframe)");
    if (flow.Button("Harden"))
    {
        MeshEditParams params;
        params.Soften = false;
        if (auto command = MeshEdit.ApplyVerb(Target(), Selection.GetSnapshot(), MeshEditVerb::SetEdgeSoftness, params))
            Commands.Execute(std::move(command));
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Back to hard (per-face) shading across the selected edges");
    if (flow.Button("Dissolve"))
    {
        if (auto command = MeshEdit.ApplyVerb(Target(), Selection.GetSnapshot(), MeshEditVerb::DissolveEdge, {}))
        {
            Commands.Execute(std::move(command));
            Selection.ClearMeshElementSelections();
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Merge the two faces sharing each selected edge.  [Backspace]");

    // Bevel: replace the selected edges with chamfer strips. Collapsed to one
    // button until clicked; the pending preview expands into its options
    // (width/segments regenerate live) until Apply/Cancel.
    const bool pendingBevel = ElementEdit.HasPendingBevel();
    if (!pendingBevel)
    {
        if (flow.Button("Bevel...") )
            BeginBevel(BevelWidth, BevelSegments);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Chamfer the selected edges; width and roundness\n"
                              "stay adjustable until applied.");
    }
    else
    {
        ImGui::SeparatorText("Bevel");
        bool bevelChanged = false;
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragFloat("Width", &BevelWidth, 0.05f, 0.0f, 1000.0f))
            bevelChanged = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How far the bevel retreats into each adjacent face");
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragInt("Segments", &BevelSegments, 0.1f, 1, 16))
        {
            BevelSegments = std::clamp(BevelSegments, 1, 16);
            bevelChanged = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Strips across the profile (1 = flat chamfer, more = rounder)");
        if (bevelChanged )
            ElementEdit.SetBevelParams(BevelWidth, BevelSegments);
        if (ImGui::Button("Apply##bevel") )
            CommitElementEdit();
        ImGui::SameLine();
        if (ImGui::Button("Cancel##bevel") )
            CancelElementEdit();
    }

    // Bridge: collapsed to one button until clicked; expanding reveals the
    // segment count before anything runs (a same-brush bridge commits
    // immediately, so its options must be authorable up front; a cross-brush
    // bridge previews pending with the count live until Apply).
    const bool pendingBridge = BridgeEdit.HasPending();
    if (!pendingBridge && !BridgeOptionsOpen)
    {
        if (flow.Button("Bridge..."))
            BridgeOptionsOpen = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Span two selected edge runs with quads.\n"
                              "More segments bow the bridge for curved hallways.");
    }
    else
    {
        ImGui::SeparatorText("Bridge");
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragInt("Segments", &BridgeSegments, 0.25f, 1, 64))
        {
            BridgeSegments = std::clamp(BridgeSegments, 1, 64);
            // A pending bridge previews the new count live.
            if (pendingBridge )
                BridgeEdit.SetSegments(BridgeSegments);
        }
        if (!pendingBridge)
        {
            if (ImGui::Button("Bridge"))
            {
                // Same-brush and cross-brush both route here; the action picks
                // the immediate mesh edit or the staged preview.
                Actions.Bridge(BridgeSegments);
                // A same-brush bridge commits right here; only a cross-brush
                // preview keeps the options open (for the live count + Apply).
                if (!BridgeEdit.HasPending())
                    BridgeOptionsOpen = false;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Bridge the selected edge runs.\n"
                                  "A cross-brush bridge previews until Apply.");
            ImGui::SameLine();
            if (ImGui::Button("Cancel##bridge"))
                BridgeOptionsOpen = false;
        }
        else
        {
            if (ImGui::Button("Apply##bridge") )
            {
                CommitBridge();
                BridgeOptionsOpen = false;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Commit the previewed bridge brush.");
            ImGui::SameLine();
            if (ImGui::Button("Cancel##bridge2") )
            {
                CancelBridge();
                BridgeOptionsOpen = false;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Drop the previewed bridge.  [Ctrl+Z]");
        }
    }
}

void ToolPropertiesPanel::DrawVertexVerbs()
{
    int selectedVertices = 0;
    for (const SelectableRef& ref : Selection.GetSelection())
    {
        if (ref.IsVertex())
            ++selectedVertices;
    }

    ImGui::Text("%d vertex(es) selected", selectedVertices);
    if (selectedVertices == 0)
    {
        ImGui::TextDisabled("Click a vertex to select");
        return;
    }

    const auto applyVerb = [&](MeshEditVerb verb, const MeshEditParams& params)
    {
        if (auto command = MeshEdit.ApplyVerb(Target(), Selection.GetSnapshot(), verb, params))
        {
            Commands.Execute(std::move(command));
            Selection.ClearMeshElementSelections();
        }
    };

    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("Distance##weld", &WeldDistance, 0.01f, 0.0f, 100.0f, "%.3f");
    ButtonFlow flow;
    if (flow.Button("Weld by Distance"))
    {
        MeshEditParams params;
        params.WeldDistance = WeldDistance;
        applyVerb(MeshEditVerb::WeldVertices, params);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Merge selected vertices closer than Distance to each other.");

    if (flow.Button("Snap to Grid"))
    {
        MeshEditParams params;
        if (ToolRegistry* tools = Tools())
        {
            const GridSettings& grid = tools->GetContext().Grid;
            params.GridOrigin = grid.Origin;
            params.GridAxisU = grid.AxisU;
            params.GridAxisV = grid.AxisV;
            params.GridSpacing = grid.Spacing;
        }
        applyVerb(MeshEditVerb::SnapVerticesToGrid, params);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Move selected vertices to the nearest grid point.");
}

void ToolPropertiesPanel::DrawSelectProperties()
{
    // Element mode selector (Object/Vertex/Edge/Face): the icon buttons that
    // drive MeshEditService. Order and labels come from the shared element
    // kind traits; only the glyph is a UI-local presentation choice.
    static constexpr std::array<const char*, MeshElementKindCount> kModeIcons = {
        ICON_FA_CUBE,          // Object
        ICON_FA_CIRCLE_DOT,    // Vertex
        ICON_FA_GRIP_LINES,    // Edge
        ICON_FA_VECTOR_SQUARE, // Face
    };
    {
        const float buttonSize = ImGui::GetFrameHeight() * 1.25f;
        bool first = true;
        for (MeshElementKind kind : AllMeshElementKinds())
        {
            if (!first)
                ImGui::SameLine();
            first = false;
            const bool active = MeshEdit.GetElementKind() == kind;
            const char* label = Traits(kind).Label;
            if (EditorUiSkin::Button(label, kModeIcons[static_cast<std::size_t>(kind)],
                                     ImVec2(buttonSize, buttonSize), active)
                && !active)
                MeshEdit.SetElementKind(kind);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", label);
        }
    }
    ImGui::Separator();

    switch (MeshEdit.GetElementKind())
    {
    case MeshElementKind::Object: DrawObjectVerbs(); break;
    case MeshElementKind::Face:   DrawFaceVerbs(); break;
    case MeshElementKind::Edge:   DrawEdgeVerbs(); break;
    case MeshElementKind::Vertex: DrawVertexVerbs(); break;
    default:                      ImGui::TextDisabled("No verbs for this mode yet"); break;
    }
}

void ToolPropertiesPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    ToolRegistry* tools = Tools();
    ITool* activeTool = tools != nullptr ? tools->GetActiveTool() : nullptr;
    if (activeTool == nullptr)
        return;

    // The select tool's "properties" are the element-mode verbs over the current
    // selection, which is panel work rather than tool work. Every other tool
    // draws its own settings, so a new one needs nothing here.
    if (activeTool->GetId() == "select")
        DrawSelectProperties();
    else
        activeTool->DrawProperties(tools->GetContext());
}
