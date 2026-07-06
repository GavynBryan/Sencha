#include "MeshEditPanel.h"

#include "ui/EditorUiStyle.h"
#include "ui/ScopedPanel.h"

#include "commands/CommandStack.h"
#include "document/WorldDocument.h"
#include "meshedit/IMeshEditTarget.h"
#include "meshedit/MeshEditService.h"
#include "project/MaterialLibrary.h"
#include "selection/SelectionService.h"
#include "meshedit/ActiveMaterialState.h"

#include <imgui.h>

#include <memory>

MeshEditPanel::MeshEditPanel(std::function<IMeshEditTarget*()> target,
                             SelectionService& selection,
                             MeshEditService& meshEdit,
                             CommandStack& commands,
                             WorldDocument& world,
                             ActiveMaterialState& activeMaterial,
                             std::optional<FaceMaterialClipboard>& uvClipboard,
                             ObjectActions objectActions)
    : TargetResolver(std::move(target))
    , Selection(selection)
    , MeshEdit(meshEdit)
    , Commands(commands)
    , World(world)
    , ActiveMaterial(activeMaterial)
    , UvClipboard(uvClipboard)
    , Objects(std::move(objectActions))
{
}

IMeshEditTarget& MeshEditPanel::Target() const
{
    return *TargetResolver();
}

std::string_view MeshEditPanel::GetTitle() const
{
    return "Mesh Edit";
}

void MeshEditPanel::DrawObjectVerbs()
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
        if (ImGui::Button("Recalculate Normals"))
        {
            if (auto command = MeshEdit.ApplyVerb(Target(), Selection.GetSnapshot(),
                                                  MeshEditVerb::RecalculateNormals, {}))
                Commands.Execute(std::move(command));
        }
        ImGui::TextDisabled("Re-orients every face outward (concave-safe)");

        if (Objects.Duplicate && ImGui::Button("Duplicate"))
            Objects.Duplicate();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Independent copies  [Ctrl+D]");
        ImGui::SameLine();
        if (Objects.Instance && ImGui::Button("Make Instance"))
            Objects.Instance();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Copies sharing this brush's mesh: editing any instance\n"
                              "edits them all, and baking shares one mesh asset.  [Alt+D]");

        if (Objects.HasInstancedSelection && Objects.HasInstancedSelection())
        {
            ImGui::SameLine();
            if (Objects.MakeUnique && ImGui::Button("Make Unique"))
                Objects.MakeUnique();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Break from the instance group, keeping all instanced edits.");
        }

        if (selectedBrushes >= 2 && Objects.Merge && ImGui::Button("Merge"))
            Objects.Merge();
        if (selectedBrushes >= 2 && ImGui::IsItemHovered())
            ImGui::SetTooltip("Join the selected brushes into the primary one\n"
                              "(textures keep their placement; no volume boolean).");

        if (Objects.Bake && ImGui::Button("Bake to Static Mesh"))
            Objects.Bake();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Writes a .smesh asset and swaps the brush for a placed mesh.\n"
                              "Duplicates of a baked entity share the asset (instances).\n"
                              "Reversible: the source brush stays in the level file.");
    }

    if (hasBaked && Objects.Revert)
    {
        if (ImGui::Button("Revert to Brush"))
            Objects.Revert();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Restores the baked entity's editable source brush.");
    }

    if (Objects.ExportGlb && ImGui::Button("Export .glb..."))
        Objects.ExportGlb();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Exports the selection's baked geometry as binary glTF.");
}

void MeshEditPanel::DrawFaceVerbs()
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
    if (ImGui::Button("Extrude"))
    {
        MeshEditParams params;
        params.Distance = ExtrudeDistance;
        applyVerb(MeshEditVerb::Extrude, params);
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete"))
        applyVerb(MeshEditVerb::Delete, {});
    ImGui::SameLine();
    if (ImGui::Button("Flip Normals"))
        applyVerb(MeshEditVerb::FlipFaceNormal, {});
    ImGui::SameLine();
    if (Objects.SeparateFaces && ImGui::Button("Separate"))
        Objects.SeparateFaces();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Split these faces into a new brush (the source opens where they were).");

    DrawTextureTools();

    ImGui::SeparatorText("Modify Texture");
    Uv.Draw(Target(), Selection, Commands, UvClipboard);
}

void MeshEditPanel::DrawTextureTools()
{
    ImGui::SeparatorText("Texture Tools");

    if (ActiveMaterial.Active.IsValid())
        ImGui::TextUnformatted(MaterialDisplayName(ActiveMaterial.Active.Path).c_str());
    else
        ImGui::TextDisabled("No active material (pick one in Materials,\nor Shift+RClick a face).");

    if (!ActiveMaterial.Active.IsValid())
        ImGui::BeginDisabled();
    if (ImGui::Button("Apply"))
        ApplyMaterialToSelectedFaces(Target(), Selection, Commands, ActiveMaterial.Active);
    if (!ActiveMaterial.Active.IsValid())
        ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Apply the active material to the selected faces.  [Shift+T]");

    ImGui::SameLine();
    if (ImGui::Button("Clear"))
        ApplyMaterialToSelectedFaces(Target(), Selection, Commands, AssetRef{});
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Drop the faces' own material; they inherit the level default.");

    ImGui::TextDisabled("Level default: %s", World.FocusDocument().GetDefaultMaterial().Path.c_str());
}

void MeshEditPanel::DrawEdgeVerbs()
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
    if (ImGui::Button("Insert Cut"))
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
}

void MeshEditPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    switch (MeshEdit.GetElementKind())
    {
    case MeshElementKind::Object: DrawObjectVerbs(); break;
    case MeshElementKind::Face:   DrawFaceVerbs(); break;
    case MeshElementKind::Edge:   DrawEdgeVerbs(); break;
    default:                      ImGui::TextDisabled("No verbs for this mode yet"); break;
    }
}
