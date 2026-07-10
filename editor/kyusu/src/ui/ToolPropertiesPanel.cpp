#include "ToolPropertiesPanel.h"

#include "ui/EditorUiSkin.h"
#include "ui/EditorUiStyle.h"
#include "ui/ScopedPanel.h"

#include "fonts/IconsFontAwesome6.h"

#include "commands/CommandStack.h"
#include "document/BrushCreationSettings.h"
#include "document/EdgeCutSettings.h"
#include "document/WorldDocument.h"
#include "document/tools/BrushTool.h"
#include "document/tools/FaceCarveTool.h"
#include "input/InputEvent.h"
#include "meshedit/IMeshEditTarget.h"
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
class ButtonFlow
{
public:
    ButtonFlow()
        : RightEdge(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x)
    {
    }

    bool Button(const char* label)
    {
        Place(label);
        return ImGui::Button(label);
    }

private:
    void Place(const char* label)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float width = ImGui::CalcTextSize(label, nullptr, true).x + style.FramePadding.x * 2.0f;
        if (!FirstInRow && ImGui::GetItemRectMax().x + style.ItemSpacing.x + width <= RightEdge)
            ImGui::SameLine();
        FirstInRow = false;
    }

    float RightEdge = 0.0f;
    bool FirstInRow = true;
};
}

ToolPropertiesPanel::ToolPropertiesPanel(std::function<IMeshEditTarget*()> target,
                                         std::function<ToolRegistry*()> tools,
                                         SelectionService& selection,
                                         MeshEditService& meshEdit,
                                         CommandStack& commands,
                                         WorldDocument& world,
                                         ActiveMaterialState& activeMaterial,
                                         std::optional<FaceMaterialClipboard>& uvClipboard,
                                         BrushCreationSettings& brushCreate,
                                         EdgeCutSettings& edgeCut,
                                         ObjectActions objectActions)
    : TargetResolver(std::move(target))
    , ToolsResolver(std::move(tools))
    , Selection(selection)
    , MeshEdit(meshEdit)
    , Commands(commands)
    , World(world)
    , ActiveMaterial(activeMaterial)
    , UvClipboard(uvClipboard)
    , BrushCreate(brushCreate)
    , EdgeCut(edgeCut)
    , Objects(std::move(objectActions))
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
    const bool hasBaked = Objects.HasBakedSelection && Objects.HasBakedSelection();

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

        if (Objects.Duplicate && flow.Button("Duplicate"))
            Objects.Duplicate();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Independent copies  [Ctrl+D]");
        if (Objects.Instance && flow.Button("Make Instance"))
            Objects.Instance();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Copies sharing this brush's mesh: editing any instance\n"
                              "edits them all, and baking shares one mesh asset.  [Alt+D]");

        if (Objects.HasInstancedSelection && Objects.HasInstancedSelection())
        {
            if (Objects.MakeUnique && flow.Button("Make Unique"))
                Objects.MakeUnique();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Break from the instance group, keeping all instanced edits.");
        }

        if (selectedBrushes >= 2 && Objects.Merge && flow.Button("Merge"))
            Objects.Merge();
        if (selectedBrushes >= 2 && ImGui::IsItemHovered())
            ImGui::SetTooltip("Join the selected brushes into the primary one\n"
                              "(textures keep their placement; no volume boolean).");

        if (Objects.Bake && flow.Button("Bake to Static Mesh"))
            Objects.Bake();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Writes a .smesh asset and swaps the brush for a placed mesh.\n"
                              "Duplicates of a baked entity share the asset (instances).\n"
                              "Reversible: the source brush stays in the level file.");

        if (hasBaked && Objects.Revert)
        {
            if (flow.Button("Revert to Brush"))
                Objects.Revert();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Restores the baked entity's editable source brush.");
        }

        if (Objects.ExportGlb && flow.Button("Export .glb..."))
            Objects.ExportGlb();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Exports the selection's baked geometry as binary glTF.");
        return;
    }

    ButtonFlow flow;
    if (hasBaked && Objects.Revert)
    {
        if (flow.Button("Revert to Brush"))
            Objects.Revert();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Restores the baked entity's editable source brush.");
    }

    if (Objects.ExportGlb && flow.Button("Export .glb..."))
        Objects.ExportGlb();
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

    ImGui::DragFloat("Distance", &ExtrudeDistance, 0.05f);
    ButtonFlow flow;
    if (flow.Button("Extrude"))
    {
        MeshEditParams params;
        params.Distance = ExtrudeDistance;
        applyVerb(MeshEditVerb::Extrude, params);
    }
    if (flow.Button("Delete"))
        applyVerb(MeshEditVerb::Delete, {});
    if (flow.Button("Flip Normals"))
        applyVerb(MeshEditVerb::FlipFaceNormal, {});
    if (Objects.SeparateFaces && flow.Button("Separate"))
        Objects.SeparateFaces();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Split these faces into a new brush (the source opens where they were).");

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
    if (flow.Button("Apply"))
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

    // Edge cut: author where along the edge to cut, and whether it rings the whole
    // loop or splits just this edge.
    ImGui::SliderFloat("Cut position", &CutPosition, 0.0f, 1.0f, "%.2f");
    ImGui::Checkbox("Loop cut", &CutLoop);
    ImGui::SameLine();
    ImGui::TextDisabled(CutLoop ? "(full ring)" : "(single edge)");
    ButtonFlow flow;
    if (flow.Button("Insert Cut"))
    {
        MeshEditParams params;
        params.CutPosition = CutPosition;
        params.LoopCut = CutLoop;
        if (auto command = MeshEdit.ApplyVerb(Target(), Selection.GetSnapshot(), MeshEditVerb::InsertEdgeLoop, params))
        {
            Commands.Execute(std::move(command));
            Selection.ClearMeshElementSelections();
        }
    }
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

    ImGui::SeparatorText("Bridge");
    const bool pendingBridge = Objects.HasPendingBridge && Objects.HasPendingBridge();
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::DragInt("Segments", &BridgeSegments, 0.25f, 1, 64))
    {
        BridgeSegments = std::clamp(BridgeSegments, 1, 64);
        // A pending bridge previews the new count live.
        if (pendingBridge && Objects.SetBridgeSegments)
            Objects.SetBridgeSegments(BridgeSegments);
    }
    if (!pendingBridge)
    {
        if (ImGui::Button("Bridge"))
        {
            if (Objects.BridgeEdges)
            {
                Objects.BridgeEdges(BridgeSegments);
            }
            else
            {
                MeshEditParams params;
                params.BridgeSegments = BridgeSegments;
                if (auto command = MeshEdit.ApplyVerb(Target(), Selection.GetSnapshot(), MeshEditVerb::BridgeEdges, params))
                {
                    Commands.Execute(std::move(command));
                    Selection.ClearMeshElementSelections();
                }
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Span two selected edge runs with quads.\n"
                              "More segments bow the bridge for curved hallways.\n"
                              "A cross-brush bridge previews until Apply.");
    }
    else
    {
        if (ImGui::Button("Apply") && Objects.CommitBridge)
            Objects.CommitBridge();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Commit the previewed bridge brush.");
        ImGui::SameLine();
        if (ImGui::Button("Cancel") && Objects.CancelBridge)
            Objects.CancelBridge();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Drop the previewed bridge.  [Ctrl+Z]");
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

void ToolPropertiesPanel::DrawBrushProperties()
{
    ImGui::SeparatorText("Primitive");

    ToolRegistry* tools = Tools();
    auto* brush = tools != nullptr ? static_cast<BrushTool*>(tools->GetActiveTool()) : nullptr;
    const bool pending = brush != nullptr && brush->HasPending();
    bool changed = false;

    struct PrimitiveOption { BrushPrimitive Kind; const char* Label; };
    static constexpr PrimitiveOption kPrimitives[] = {
        { BrushPrimitive::Box, "Box" },
        { BrushPrimitive::Plane, "Plane" },
        { BrushPrimitive::Cylinder, "Cylinder" },
    };
    {
        ButtonFlow flow;
        for (const PrimitiveOption& prim : kPrimitives)
        {
            const bool active = BrushCreate.ActivePrimitive == prim.Kind;
            if (active)
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (flow.Button(prim.Label) && !active)
            {
                BrushCreate.ActivePrimitive = prim.Kind;
                changed = true;
            }
            if (active)
                ImGui::PopStyleColor();
        }
    }

    if (BrushCreate.ActivePrimitive == BrushPrimitive::Cylinder)
    {
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragInt("Sides", &BrushCreate.CylinderSides, 0.25f, 3, 64))
        {
            BrushCreate.CylinderSides = std::clamp(BrushCreate.CylinderSides, 3, 64);
            changed = true;
        }
    }

    if (BrushCreate.ActivePrimitive == BrushPrimitive::Plane)
    {
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragInt("Subdivisions", &BrushCreate.PlaneSubdivisions, 0.25f, 1, 32))
        {
            BrushCreate.PlaneSubdivisions = std::clamp(BrushCreate.PlaneSubdivisions, 1, 32);
            changed = true;
        }
    }

    if (ImGui::RadioButton("Outer", !BrushCreate.Inner))
    {
        changed |= BrushCreate.Inner;
        BrushCreate.Inner = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Inner", BrushCreate.Inner))
    {
        changed |= !BrushCreate.Inner;
        BrushCreate.Inner = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Inside-out brush (all normals flipped), for rooms seen from within");

    if (changed && pending)
        brush->RefreshPending(tools->GetContext());

    if (!pending)
    {
        ImGui::TextDisabled("Drag in a viewport to create");
        return;
    }

    // Clicking synthesizes the exact key path (ToolRegistry routes KeyDownEvent
    // to the active tool), so commit/cancel have ONE code path shared with the
    // viewport keys and click-off.
    ButtonFlow flow;
    if (flow.Button("Apply") && tools != nullptr)
        tools->OnInput(InputEvent{ KeyDownEvent{ SDLK_RETURN, {} } });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Commit the pending brush  [Enter]");
    if (flow.Button("Cancel") && tools != nullptr)
        tools->OnInput(InputEvent{ KeyDownEvent{ SDLK_ESCAPE, {} } });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Discard the pending brush  [Esc]");
}

void ToolPropertiesPanel::DrawEdgeCutProperties()
{
    ImGui::SeparatorText("Edge Cut");

    if (ImGui::RadioButton("Loop cut (whole ring)", EdgeCut.LoopCut))
        EdgeCut.LoopCut = true;
    if (ImGui::RadioButton("Single edge cut", !EdgeCut.LoopCut))
        EdgeCut.LoopCut = false;
    ImGui::TextDisabled("Tab toggles");
}

void ToolPropertiesPanel::DrawFaceCarveProperties()
{
    ImGui::SeparatorText("Face Carve");

    ToolRegistry* tools = Tools();
    auto* carve = tools != nullptr
        ? static_cast<FaceCarveTool*>(tools->GetActiveTool())
        : nullptr;
    const bool hasDraft = carve != nullptr && carve->HasPending();
    const bool canCommit = carve != nullptr && carve->CanCommit();

    if (carve != nullptr)
    {
        const FaceCarveMode mode = carve->GetMode();
        if (ImGui::RadioButton("Inset", mode == FaceCarveMode::Inset))
            carve->SetMode(tools->GetContext(), FaceCarveMode::Inset);
        if (ImGui::RadioButton("Edge Loop", mode == FaceCarveMode::EdgeLoop))
            carve->SetMode(tools->GetContext(), FaceCarveMode::EdgeLoop);

        bool pierce = carve->IsPierceEnabled();
        if (ImGui::Checkbox("Pierce", &pierce))
            carve->SetPierceEnabled(tools->GetContext(), pierce);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Carve matching bounds into the nearest opposite face");
    }

    if (!hasDraft)
    {
        if (carve != nullptr && carve->GetMode() == FaceCarveMode::EdgeLoop)
            ImGui::TextDisabled("Drag loop bounds on a face");
        else
            ImGui::TextDisabled("Drag an inset on a face");
    }

    // Clicking synthesizes the exact key path (ToolRegistry routes KeyDownEvent
    // to the active tool), so commit/cancel have ONE code path shared with the
    // toolbar buttons and the viewport keys.
    if (!canCommit)
        ImGui::BeginDisabled();
    ButtonFlow flow;
    if (flow.Button("Apply") && tools != nullptr)
        tools->OnInput(InputEvent{ KeyDownEvent{ SDLK_RETURN, {} } });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Commit the pending carve  [Enter]");
    if (!canCommit)
        ImGui::EndDisabled();
    if (!hasDraft)
        ImGui::BeginDisabled();
    if (flow.Button("Cancel") && tools != nullptr)
        tools->OnInput(InputEvent{ KeyDownEvent{ SDLK_ESCAPE, {} } });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Discard the pending carve  [Esc]");
    if (!hasDraft)
        ImGui::EndDisabled();
}

void ToolPropertiesPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    const ToolRegistry* tools = Tools();
    const ITool* activeTool = tools != nullptr ? tools->GetActiveTool() : nullptr;
    const std::string_view toolId = activeTool != nullptr ? activeTool->GetId() : std::string_view{};

    if (toolId == "brush")
        DrawBrushProperties();
    else if (toolId == "edgecut")
        DrawEdgeCutProperties();
    else if (toolId == "facecarve")
        DrawFaceCarveProperties();
    else
        DrawSelectProperties();
}
