#include "MaterialPanel.h"

#include "ui/EditorUiStyle.h"
#include "ui/ScopedPanel.h"

#include "commands/CommandStack.h"
#include "document/EditorDocument.h"
#include "project/MaterialLibrary.h"
#include "brush/BrushMesh.h"
#include "brush/FaceMaterial.h"
#include "meshedit/IMeshEditTarget.h"
#include "meshedit/MeshEditService.h"
#include "selection/SelectionService.h"

#include <ecs/EntityId.h>

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace
{
    // A face's loop vertices in brush-local space — what the UV justify presets
    // operate on (the math itself lives in FaceMaterial, kernel-side).
    std::vector<Vec3d> FaceLocalPositions(const BrushMesh& mesh, const BrushFace& face)
    {
        std::vector<Vec3d> positions;
        positions.reserve(face.Loop.size());
        for (std::uint32_t index : face.Loop)
            positions.push_back(mesh.Vertices[index].Position);
        return positions;
    }

    // Newell's method over world-space corners: direction is the face normal,
    // magnitude twice the area, so summing across faces area-weights the average.
    Vec3d WorldFaceAreaNormal(std::span<const Vec3d> corners)
    {
        Vec3d normal{ 0.0f, 0.0f, 0.0f };
        const std::size_t n = corners.size();
        for (std::size_t i = 0; i < n; ++i)
        {
            const Vec3d& a = corners[i];
            const Vec3d& b = corners[(i + 1) % n];
            normal.X += (a.Y - b.Y) * (a.Z + b.Z);
            normal.Y += (a.Z - b.Z) * (a.X + b.X);
            normal.Z += (a.X - b.X) * (a.Y + b.Y);
        }
        return normal;
    }
}

MaterialPanel::MaterialPanel(IMeshEditTarget& target,
                             SelectionService& selection,
                             MeshEditService& meshEdit,
                             CommandStack& commands,
                             MaterialLibrary& materials,
                             EditorDocument& document)
    : Target(target)
    , Selection(selection)
    , MeshEdit(meshEdit)
    , Commands(commands)
    , Materials(materials)
    , Document(document)
{
}

std::string_view MaterialPanel::GetTitle() const
{
    return "Material";
}

std::optional<UvProjection> MaterialPanel::RepresentativeUv() const
{
    for (const SelectableRef& ref : Selection.GetSelection())
    {
        if (!ref.IsFace())
            continue;
        const std::optional<MeshEditTargetMesh> resolved = Target.Resolve(ref.Entity);
        if (resolved && resolved->Mesh && ref.ElementId < resolved->Mesh->Faces.size())
            return resolved->Mesh->Faces[ref.ElementId].Material.Uv;
    }
    return std::nullopt;
}

void MaterialPanel::EditSelectedFaces(
    const std::function<void(const BrushMesh&, const Transform3f&, BrushFace&)>& mutate)
{
    // Group selected face indices by entity so a multi-face edit on one brush is
    // a single command (value-semantics before/after).
    std::vector<std::pair<EntityId, std::vector<std::uint32_t>>> byEntity;
    for (const SelectableRef& ref : Selection.GetSelection())
    {
        if (!ref.IsFace())
            continue;
        auto it = std::find_if(byEntity.begin(), byEntity.end(),
                               [&](const auto& e) { return e.first == ref.Entity; });
        if (it == byEntity.end())
            byEntity.push_back({ ref.Entity, { ref.ElementId } });
        else
            it->second.push_back(ref.ElementId);
    }

    for (auto& [entity, faces] : byEntity)
    {
        const std::optional<MeshEditTargetMesh> resolved = Target.Resolve(entity);
        if (!resolved || !resolved->Mesh)
            continue;

        BrushMesh before = *resolved->Mesh;
        BrushMesh after = before;
        bool changed = false;
        for (std::uint32_t f : faces)
        {
            if (f < after.Faces.size())
            {
                mutate(after, resolved->Transform, after.Faces[f]);
                changed = true;
            }
        }
        if (changed)
        {
            if (auto command = Target.MakeEditCommand(entity, std::move(before), std::move(after)))
                Commands.Execute(std::move(command));
        }
    }
}

void MaterialPanel::JustifySelectionAsOne(bool fit)
{
    // Union pass: every selected face's world loop positions and the
    // area-weighted average world normal.
    std::vector<Vec3d> worldPositions;
    std::vector<Vec3d> corners;
    Vec3d normalSum{ 0.0f, 0.0f, 0.0f };
    for (const SelectableRef& ref : Selection.GetSelection())
    {
        if (!ref.IsFace())
            continue;
        const std::optional<MeshEditTargetMesh> resolved = Target.Resolve(ref.Entity);
        if (!resolved || !resolved->Mesh || ref.ElementId >= resolved->Mesh->Faces.size())
            continue;
        const BrushFace& face = resolved->Mesh->Faces[ref.ElementId];
        corners.clear();
        for (std::uint32_t index : face.Loop)
            corners.push_back(resolved->Transform.TransformPoint(resolved->Mesh->Vertices[index].Position));
        normalSum = normalSum + WorldFaceAreaNormal(corners);
        worldPositions.insert(worldPositions.end(), corners.begin(), corners.end());
    }
    if (worldPositions.empty())
        return;
    if (normalSum.SqrMagnitude() <= 0.0f)
        normalSum = { 0.0f, 1.0f, 0.0f };

    // One world projection for the whole selection: box-mapping axes from the
    // dominant axis of the average normal (UvProjectionForNormal's world-aligned
    // axes ARE world axes), density seeded from the representative face so
    // Center keeps the current tiling.
    const UvProjection axes = UvProjectionForNormal(normalSum, /*worldAligned*/ true);
    WorldUvProjection world;
    world.AxisU = axes.AxisU;
    world.AxisV = axes.AxisV;
    if (const std::optional<UvProjection> representative = RepresentativeUv())
        world.Scale = representative->Scale;
    world = fit ? WorldUvProjectionFit(world, worldPositions)
                : WorldUvProjectionCenter(world, worldPositions);

    // Every face takes the SAME world mapping, baked into its brush's frame, so
    // the texture flows continuously across faces and across brushes.
    EditSelectedFaces([&](const BrushMesh&, const Transform3f& transform, BrushFace& f) {
        f.Material.Uv = UvProjectionToLocal(world, transform);
    });
}

void MaterialPanel::CopyProjection()
{
    const SelectableRef primary = Selection.GetPrimarySelection();
    SelectableRef source = primary.IsFace() ? primary : SelectableRef{};
    if (!source.IsValid())
    {
        for (const SelectableRef& ref : Selection.GetSelection())
            if (ref.IsFace())
            {
                source = ref;
                break;
            }
    }
    if (!source.IsValid())
        return;

    const std::optional<MeshEditTargetMesh> resolved = Target.Resolve(source.Entity);
    if (!resolved || !resolved->Mesh || source.ElementId >= resolved->Mesh->Faces.size())
        return;

    const FaceMaterial& face = resolved->Mesh->Faces[source.ElementId].Material;
    Copied = CopiedFaceMaterial{
        .Projection = UvProjectionToWorld(face.Uv, resolved->Transform),
        .Material = face.Material,
    };
}

void MaterialPanel::PasteProjection()
{
    if (!Copied.has_value())
        return;
    EditSelectedFaces([&](const BrushMesh&, const Transform3f& transform, BrushFace& f) {
        f.Material.Material = Copied->Material;
        f.Material.Uv = UvProjectionToLocal(Copied->Projection, transform);
    });
}

void MaterialPanel::DrawMaterialPicker()
{
    ImGui::SeparatorText("Material");

    const std::vector<MaterialAsset>& materials = Materials.Materials();
    if (materials.empty())
    {
        ImGui::TextDisabled("No .smat materials found.");
        ImGui::TextDisabled("Save the level next to a materials/ folder, then Rescan.");
    }

    if (ImGui::BeginListBox("##materials", ImVec2(-FLT_MIN, 6 * ImGui::GetTextLineHeightWithSpacing())))
    {
        for (const MaterialAsset& entry : materials)
        {
            if (ImGui::Selectable(entry.DisplayName.c_str()))
            {
                const AssetRef material{ AssetType::Material, entry.Path };
                EditSelectedFaces([&](const BrushMesh&, const Transform3f&, BrushFace& face) {
                    face.Material.Material = material;
                });
            }
        }
        ImGui::EndListBox();
    }

    if (ImGui::Button("Clear (use level default)"))
        EditSelectedFaces([](const BrushMesh&, const Transform3f&, BrushFace& face) { face.Material.Material = {}; });

    ImGui::TextDisabled("Level default: %s", Document.GetDefaultMaterial().Path.c_str());
}

void MaterialPanel::DrawUvControls(const UvProjection& current)
{
    ImGui::SeparatorText("UV Projection");

    // Reseed the drag buffer from the selection only while not mid-edit, so a
    // drag accumulates against its own running value (reseeding every frame would
    // pin it to the unchanged scene value). Commit once, per field, when the drag
    // ends — heterogeneous multi-selections keep their untouched fields.
    if (!UvEditing)
        UvEdit = current;

    ImGui::DragFloat2("Scale", &UvEdit.Scale.X, 0.01f, 0.0f, 0.0f, "%.3f");
    if (ImGui::IsItemActivated())
        UvEditing = true;
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        const Vec2d scale = UvEdit.Scale;
        EditSelectedFaces([&](const BrushMesh&, const Transform3f&, BrushFace& f) { f.Material.Uv.Scale = scale; });
        UvEditing = false;
    }

    ImGui::DragFloat2("Offset", &UvEdit.Offset.X, 0.005f);
    if (ImGui::IsItemActivated())
        UvEditing = true;
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        const Vec2d offset = UvEdit.Offset;
        EditSelectedFaces([&](const BrushMesh&, const Transform3f&, BrushFace& f) { f.Material.Uv.Offset = offset; });
        UvEditing = false;
    }

    ImGui::DragFloat("Rotation", &UvEdit.Rotation, 0.5f, 0.0f, 0.0f, "%.1f deg");
    if (ImGui::IsItemActivated())
        UvEditing = true;
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        const float rotation = UvEdit.Rotation;
        EditSelectedFaces([&](const BrushMesh&, const Transform3f&, BrushFace& f) { f.Material.Uv.Rotation = rotation; });
        UvEditing = false;
    }

    bool worldAligned = current.WorldAligned;
    if (ImGui::Checkbox("World-aligned", &worldAligned))
    {
        // Toggling alignment re-derives the axes from each face's own normal.
        EditSelectedFaces([&](const BrushMesh& mesh, const Transform3f&, BrushFace& f) {
            const Vec3d normal = BrushComputeFaceNormal(mesh, f);
            f.Material.Uv = UvProjectionForNormal(normal, worldAligned);
        });
    }

    if (ImGui::Button("Fit"))
    {
        if (TreatAsOne)
            JustifySelectionAsOne(/*fit*/ true);
        else
            EditSelectedFaces([](const BrushMesh& mesh, const Transform3f&, BrushFace& f) {
                f.Material.Uv = UvProjectionFit(f.Material.Uv, FaceLocalPositions(mesh, f));
            });
    }
    ImGui::SameLine();
    if (ImGui::Button("Center"))
    {
        if (TreatAsOne)
            JustifySelectionAsOne(/*fit*/ false);
        else
            EditSelectedFaces([](const BrushMesh& mesh, const Transform3f&, BrushFace& f) {
                f.Material.Uv = UvProjectionCenter(f.Material.Uv, FaceLocalPositions(mesh, f));
            });
    }
    ImGui::SameLine();
    ImGui::Checkbox("Treat as one", &TreatAsOne);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Fit/Center the whole selection as a single unit:\n"
                          "one continuous mapping across faces and brushes.");

    if (ImGui::Button("Copy projection"))
        CopyProjection();
    ImGui::SameLine();
    if (!Copied.has_value())
        ImGui::BeginDisabled();
    if (ImGui::Button("Paste projection"))
        PasteProjection();
    if (!Copied.has_value())
        ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Apply the copied material + mapping in world space:\n"
                          "the texture lands identically across brushes.");
    ImGui::SameLine();
    if (ImGui::Button("Reset axes"))
        EditSelectedFaces([](const BrushMesh& mesh, const Transform3f&, BrushFace& f) {
            const Vec3d normal = BrushComputeFaceNormal(mesh, f);
            f.Material.Uv = UvProjectionForNormal(normal, f.Material.Uv.WorldAligned);
        });
}

void MaterialPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    if (MeshEdit.GetElementKind() != MeshElementKind::Face)
    {
        ImGui::TextDisabled("Switch to Face mode to texture faces.");
        return;
    }

    if (ImGui::Button("Rescan materials"))
        Materials.Rescan(Materials.Roots());
    ImGui::SameLine();
    ImGui::TextDisabled("%zu found", Materials.Materials().size());

    const std::optional<UvProjection> representativeUv = RepresentativeUv();
    if (!representativeUv.has_value())
    {
        UvEditing = false; // nothing to edit; don't strand a half-finished drag
        ImGui::Separator();
        ImGui::TextDisabled("Select one or more faces to texture.");
        return;
    }

    DrawMaterialPicker();
    DrawUvControls(*representativeUv);
}
