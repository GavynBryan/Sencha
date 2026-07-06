#include "FaceUvControls.h"

#include "brush/BrushMesh.h"
#include "commands/CommandStack.h"
#include "meshedit/IMeshEditTarget.h"
#include "selection/SelectionService.h"

#include <imgui.h>

#include <utility>

void FaceUvControls::Draw(IMeshEditTarget& target,
                          const SelectionService& selection,
                          CommandStack& commands,
                          std::optional<FaceMaterialClipboard>& clipboard)
{
    const std::optional<UvProjection> representative = RepresentativeFaceUv(target, selection);
    if (!representative.has_value())
    {
        UvEditing = false; // nothing to edit; don't strand a half-finished drag
        ImGui::TextDisabled("Select one or more faces.");
        return;
    }

    // Reseed the drag buffer from the selection only while not mid-edit, so a
    // drag accumulates against its own running value (reseeding every frame would
    // pin it to the unchanged scene value). Commit once, per field, when the drag
    // ends — heterogeneous multi-selections keep their untouched fields.
    if (!UvEditing)
        UvEdit = *representative;

    // Align: which basis the projection axes derive from. Switching re-derives
    // the axes from each face's own normal.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Align");
    ImGui::SameLine();
    const bool worldAligned = representative->WorldAligned;
    if (ImGui::RadioButton("World", worldAligned) && !worldAligned)
        EditSelectedFaces(target, selection, commands,
                          [](const BrushMesh& mesh, const Transform3f&, BrushFace& f) {
                              f.Material.Uv = UvProjectionForNormal(BrushComputeFaceNormal(mesh, f), true);
                          });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Project from world box axes: resize keeps the texture pinned.");
    ImGui::SameLine();
    if (ImGui::RadioButton("Face", !worldAligned) && worldAligned)
        EditSelectedFaces(target, selection, commands,
                          [](const BrushMesh& mesh, const Transform3f&, BrushFace& f) {
                              f.Material.Uv = UvProjectionForNormal(BrushComputeFaceNormal(mesh, f), false);
                          });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Lock the projection to the face as it deforms.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset axes"))
        EditSelectedFaces(target, selection, commands,
                          [](const BrushMesh& mesh, const Transform3f&, BrushFace& f) {
                              f.Material.Uv = UvProjectionForNormal(BrushComputeFaceNormal(mesh, f),
                                                                    f.Material.Uv.WorldAligned);
                          });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Re-derive the projection axes from each face's normal.");

    ImGui::DragFloat2("Scale", &UvEdit.Scale.X, 0.01f, 0.0f, 0.0f, "%.3f");
    if (ImGui::IsItemActivated())
        UvEditing = true;
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        const Vec2d scale = UvEdit.Scale;
        EditSelectedFaces(target, selection, commands,
                          [&](const BrushMesh&, const Transform3f&, BrushFace& f) { f.Material.Uv.Scale = scale; });
        UvEditing = false;
    }

    ImGui::DragFloat2("Shift", &UvEdit.Offset.X, 0.005f);
    if (ImGui::IsItemActivated())
        UvEditing = true;
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        const Vec2d offset = UvEdit.Offset;
        EditSelectedFaces(target, selection, commands,
                          [&](const BrushMesh&, const Transform3f&, BrushFace& f) { f.Material.Uv.Offset = offset; });
        UvEditing = false;
    }

    ImGui::DragFloat("Rotate", &UvEdit.Rotation, 0.5f, 0.0f, 0.0f, "%.1f deg");
    if (ImGui::IsItemActivated())
        UvEditing = true;
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        const float rotation = UvEdit.Rotation;
        EditSelectedFaces(target, selection, commands,
                          [&](const BrushMesh&, const Transform3f&, BrushFace& f) { f.Material.Uv.Rotation = rotation; });
        UvEditing = false;
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Justify");
    ImGui::SameLine();
    if (ImGui::SmallButton("Fit"))
    {
        if (TreatAsOne)
            JustifySelectedFacesAsOne(target, selection, commands, /*fit*/ true);
        else
            EditSelectedFaces(target, selection, commands,
                              [](const BrushMesh& mesh, const Transform3f&, BrushFace& f) {
                                  f.Material.Uv = UvProjectionFit(f.Material.Uv, FaceLocalPositions(mesh, f));
                              });
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Center"))
    {
        if (TreatAsOne)
            JustifySelectedFacesAsOne(target, selection, commands, /*fit*/ false);
        else
            EditSelectedFaces(target, selection, commands,
                              [](const BrushMesh& mesh, const Transform3f&, BrushFace& f) {
                                  f.Material.Uv = UvProjectionCenter(f.Material.Uv, FaceLocalPositions(mesh, f));
                              });
    }
    ImGui::SameLine();
    ImGui::Checkbox("Treat as one", &TreatAsOne);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Fit/Center the whole selection as a single unit:\n"
                          "one continuous mapping across faces and brushes.");

    if (ImGui::SmallButton("Copy projection"))
    {
        if (auto copied = CopySelectedFaceProjection(target, selection))
            clipboard = std::move(copied);
    }
    ImGui::SameLine();
    if (!clipboard.has_value())
        ImGui::BeginDisabled();
    if (ImGui::SmallButton("Paste projection"))
        PasteFaceProjection(target, selection, commands, *clipboard);
    if (!clipboard.has_value())
        ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Apply the copied material + mapping in world space:\n"
                          "the texture lands identically across brushes.");
}
