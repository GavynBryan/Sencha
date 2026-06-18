#include "MeshEditPanel.h"

#include "EditorUiSkin.h"
#include "EditorUiStyle.h"

#include "../commands/CommandStack.h"
#include "../meshedit/IMeshEditTarget.h"
#include "../meshedit/MeshEditService.h"
#include "../selection/SelectionService.h"

#include <imgui.h>

#include <memory>

MeshEditPanel::MeshEditPanel(IMeshEditTarget& target,
                             SelectionService& selection,
                             MeshEditService& meshEdit,
                             CommandStack& commands)
    : Target(target)
    , Selection(selection)
    , MeshEdit(meshEdit)
    , Commands(commands)
{
}

std::string_view MeshEditPanel::GetTitle() const
{
    return "Mesh Edit";
}

void MeshEditPanel::DrawModeToolbar()
{
    const auto modeButton = [&](const char* label, MeshElementKind kind)
    {
        const bool active = MeshEdit.GetElementKind() == kind;
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button, EditorUi::AccentDim);
        if (ImGui::Button(label))
            MeshEdit.SetElementKind(kind);
        if (active)
            ImGui::PopStyleColor();
    };

    modeButton("Object", MeshElementKind::Object);
    ImGui::SameLine();
    modeButton("Vertex", MeshElementKind::Vertex);
    ImGui::SameLine();
    modeButton("Edge", MeshElementKind::Edge);
    ImGui::SameLine();
    modeButton("Face", MeshElementKind::Face);

    ImGui::TextDisabled("Shift+V cycles modes");
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
        if (auto command = MeshEdit.ApplyVerb(Target, Selection.GetSnapshot(), verb, params))
            Commands.Execute(std::move(command));
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

    if (ImGui::Button("Split"))
    {
        if (auto command = MeshEdit.ApplyVerb(Target, Selection.GetSnapshot(), MeshEditVerb::SplitEdge, {}))
            Commands.Execute(std::move(command));
    }
}

void MeshEditPanel::OnDraw()
{
    if (!ImGui::Begin(GetTitle().data(), &Visible))
    {
        ImGui::End();
        return;
    }
    EditorUiSkin::PanelBackdrop();

    DrawModeToolbar();
    ImGui::Separator();

    switch (MeshEdit.GetElementKind())
    {
    case MeshElementKind::Face: DrawFaceVerbs(); break;
    case MeshElementKind::Edge: DrawEdgeVerbs(); break;
    default:                    ImGui::TextDisabled("No verbs for this mode yet"); break;
    }

    ImGui::End();
}
