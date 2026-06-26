#include "MeshEditPanel.h"

#include "EditorUiStyle.h"
#include "ScopedPanel.h"

#include "../commands/CommandStack.h"
#include "../editmodes/ManipulatorSession.h"
#include "../meshedit/IMeshEditTarget.h"
#include "../meshedit/MeshEditService.h"
#include "../selection/SelectionService.h"

#include <imgui.h>

#include <memory>

MeshEditPanel::MeshEditPanel(IMeshEditTarget& target,
                             SelectionService& selection,
                             MeshEditService& meshEdit,
                             CommandStack& commands,
                             ManipulatorSession& manipulators,
                             std::function<void()> setOriginToPivot)
    : Target(target)
    , Selection(selection)
    , MeshEdit(meshEdit)
    , Commands(commands)
    , Manipulators(manipulators)
    , SetOriginToPivot(std::move(setOriginToPivot))
{
}

std::string_view MeshEditPanel::GetTitle() const
{
    return "Mesh Edit";
}

void MeshEditPanel::DrawGizmoToolbar()
{
    const auto gizmoButton = [&](const char* label, TransformMode mode)
    {
        const bool active = Manipulators.GetTransformMode() == mode;
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button, EditorUi::AccentDim);
        if (ImGui::Button(label))
            Manipulators.SetTransformMode(mode);
        if (active)
            ImGui::PopStyleColor();
    };

    gizmoButton("Resize", TransformMode::Resize);
    ImGui::SameLine();
    gizmoButton("Move", TransformMode::Move);
    ImGui::SameLine();
    gizmoButton("Rotate", TransformMode::Rotate);
    ImGui::SameLine();
    gizmoButton("Scale", TransformMode::Scale);

    // Pivot edit: the Move gizmo moves the transient rotate/scale center instead of
    // the selection. Persists until the selection changes.
    const bool editingPivot = Manipulators.IsEditingPivot();
    if (editingPivot)
        ImGui::PushStyleColor(ImGuiCol_Button, EditorUi::AccentDim);
    if (ImGui::Button(editingPivot ? "Pivot: editing" : "Edit Pivot"))
        Manipulators.SetEditingPivot(!editingPivot);
    if (editingPivot)
        ImGui::PopStyleColor();

    // Bake the moved pivot into the brush's origin (only meaningful once moved).
    ImGui::SameLine();
    const bool hasPivot = Manipulators.HasPivotOverride();
    if (!hasPivot)
        ImGui::BeginDisabled();
    if (ImGui::Button("Set Origin to Pivot") && SetOriginToPivot)
        SetOriginToPivot();
    if (!hasPivot)
        ImGui::EndDisabled();

    ImGui::TextDisabled("Q/W/E/R switch gizmos");
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

void MeshEditPanel::DrawObjectVerbs()
{
    int selectedBrushes = 0;
    for (const SelectableRef& ref : Selection.GetSelection())
        if (ref.IsEntity())
            ++selectedBrushes;

    if (selectedBrushes == 0)
    {
        ImGui::TextDisabled("Select a brush to edit it");
        return;
    }

    if (ImGui::Button("Recalculate Normals"))
    {
        if (auto command = MeshEdit.ApplyVerb(Target, Selection.GetSnapshot(),
                                              MeshEditVerb::RecalculateNormals, {}))
            Commands.Execute(std::move(command));
    }
    ImGui::TextDisabled("Re-orients every face outward (concave-safe)");
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
        if (auto command = MeshEdit.ApplyVerb(Target, Selection.GetSnapshot(), MeshEditVerb::InsertEdgeLoop, params))
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

    DrawGizmoToolbar();
    ImGui::Separator();

    DrawModeToolbar();
    ImGui::Separator();

    switch (MeshEdit.GetElementKind())
    {
    case MeshElementKind::Object: DrawObjectVerbs(); break;
    case MeshElementKind::Face:   DrawFaceVerbs(); break;
    case MeshElementKind::Edge:   DrawEdgeVerbs(); break;
    default:                      ImGui::TextDisabled("No verbs for this mode yet"); break;
    }
}
