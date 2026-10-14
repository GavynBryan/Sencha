#include "workspace/FaceUvSource.h"

#include "actions/ActionOffer.h"
#include "brush/BrushMesh.h"
#include "meshedit/FaceMaterialEdits.h"
#include "project/MaterialLibrary.h"
#include "properties/SettingProperties.h"
#include "workspace/BrushManipulationSink.h"
#include "workspace/EditorWorkspace.h"

#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <array>

namespace
{
constexpr std::array<std::string_view, 2> kAlignments{ "World", "Face" };

void Realign(IMeshEditTarget& target, const SelectionService& selection, CommandStack& commands, std::optional<bool> world)
{
    EditSelectedFaces(target, selection, commands, [world](const BrushMesh& mesh, const Transform3f&, BrushFace& face) {
        face.Material.Uv = UvProjectionForNormal(BrushComputeFaceNormal(mesh, face),
                                                 world.value_or(face.Material.Uv.WorldAligned));
    });
}
} // namespace

bool FaceUvSource::HasFaces() const
{
    return Workspace.Interaction.Sink != nullptr
        && RepresentativeFaceMaterial(*Workspace.Interaction.Sink, Workspace.Selection).has_value();
}

void FaceUvSource::Justify(bool fit)
{
    IMeshEditTarget* target = Workspace.Interaction.Sink.get();
    if (target == nullptr)
        return;
    if (TreatAsOne)
    {
        JustifySelectedFacesAsOne(*target, Workspace.Selection, Workspace.Commands, fit);
        return;
    }
    EditSelectedFaces(*target, Workspace.Selection, Workspace.Commands,
                      [fit](const BrushMesh& mesh, const Transform3f&, BrushFace& face) {
                          const std::vector<Vec3d> positions = FaceLocalPositions(mesh, face);
                          face.Material.Uv = fit ? UvProjectionFit(face.Material.Uv, positions)
                                                 : UvProjectionCenter(face.Material.Uv, positions);
                      });
}

void FaceUvSource::Offer(ActionCatalog& catalog, ActionContext& context)
{
    const ActionOffers offer{ catalog, context, "Texture" };
    const auto hasFaces = [this] { return HasFaces(); };
    offer({ .Id = "uv.fit", .Label = "Fit Texture" }, { .Invoke = [this] { Justify(true); }, .Enabled = hasFaces });
    offer({ .Id = "uv.center", .Label = "Center Texture" },
          { .Invoke = [this] { Justify(false); }, .Enabled = hasFaces });
    offer({ .Id = "uv.reset_axes", .Label = "Reset Texture Axes" },
          { .Invoke = [this] {
                if (Workspace.Interaction.Sink != nullptr)
                    Realign(*Workspace.Interaction.Sink, Workspace.Selection, Workspace.Commands, std::nullopt);
            },
            .Enabled = hasFaces });
    offer({ .Id = "material.copy_proj", .Label = "Copy Face Projection", .Category = "Material",
            .DefaultChords = { { SDLK_C, { .Ctrl = true, .Shift = true } } } },
          { .Invoke = [this] { Workspace.CopySelectedFaceProjection(); } });
    offer({ .Id = "material.paste_proj", .Label = "Paste Face Projection", .Category = "Material",
            .DefaultChords = { { SDLK_V, { .Ctrl = true, .Shift = true } } } },
          { .Invoke = [this] { Workspace.PasteFaceProjectionToSelection(); } });
}

std::vector<std::string> FaceUvSource::Relevant() const
{
    return { "uv.fit", "uv.center", "uv.reset_axes", "material.copy_proj", "material.paste_proj" };
}

std::vector<PropertyNode> FaceUvSource::Nodes() const
{
    if (Workspace.Interaction.Sink == nullptr)
        return {};
    const std::optional<FaceMaterial> face = RepresentativeFaceMaterial(*Workspace.Interaction.Sink, Workspace.Selection);
    if (!face.has_value())
        return {};
    const UvProjection* uv = &face->Uv;
    const std::array<double, 2> scale{ uv->Scale.X, uv->Scale.Y };
    const std::array<double, 2> shift{ uv->Offset.X, uv->Offset.Y };
    PropertyNode material{ .Key = "material",
                           .Descriptor = { .Kind = PropertyKind::Asset,
                                           .Label = "Material",
                                           .Tooltip = "Empty paints with the level's default material",
                                           .Asset = AssetType::Material },
                           .Value = face->Material.Path };
    return {
        std::move(material),
        ChoiceProperty("align", "Align", kAlignments, uv->WorldAligned ? 0 : 1,
                       "World pins the texture as the brush is resized; Face locks it to the face as it deforms"),
        VectorProperty("scale", "Scale", scale, "World units per texture tile"),
        VectorProperty("shift", "Shift", shift),
        NumberProperty("rotate", "Rotate", uv->Rotation, 0.0, -1.0, 0.5, "Degrees"),
        ToggleProperty("as_one", "Justify as one", TreatAsOne,
                       "Fit and Center map the whole selection as one continuous surface"),
    };
}

std::string FaceUvSource::ValueOf(std::string_view key) const
{
    for (const PropertyNode& node : Nodes())
        if (node.Key == key)
            return node.Value;
    return {};
}

std::vector<std::string> FaceUvSource::Candidates(std::string_view key) const
{
    std::vector<std::string> paths;
    if (key == "material" && Library != nullptr)
        for (const MaterialAsset& material : Library->Materials())
            paths.push_back(material.Path);
    return paths;
}

PropertyEditResult FaceUvSource::CommitEdit(std::string_view key, std::string_view value)
{
    return Set(key, value);
}

PropertyEditResult FaceUvSource::Set(std::string_view key, std::string_view value)
{
    IMeshEditTarget* target = Workspace.Interaction.Sink.get();
    if (target == nullptr)
        return PropertyEditResult::Gone;
    if (value == ValueOf(key))
        return PropertyEditResult::Unchanged;
    const auto edit = [&](auto mutate) {
        EditSelectedFaces(*target, Workspace.Selection, Workspace.Commands,
                          [&](const BrushMesh&, const Transform3f&, BrushFace& face) { mutate(face.Material.Uv); });
    };
    if (key == "material")
    {
        ApplyMaterialToSelectedFaces(*target, Workspace.Selection, Workspace.Commands,
                                     AssetRef{ AssetType::Material, std::string(value) });
    }
    else if (key == "align")
    {
        const std::optional<std::size_t> choice = ParseChoice(value, kAlignments);
        if (!choice.has_value())
            return PropertyEditResult::Refused;
        Realign(*target, Workspace.Selection, Workspace.Commands, *choice == 0);
    }
    else if (key == "scale" || key == "shift")
    {
        const std::optional<std::vector<double>> numbers = ParseVector(value, 2);
        if (!numbers.has_value())
            return PropertyEditResult::Refused;
        const Vec2d vector(static_cast<float>((*numbers)[0]), static_cast<float>((*numbers)[1]));
        if (key == "scale")
            edit([vector](UvProjection& uv) { uv.Scale = vector; });
        else
            edit([vector](UvProjection& uv) { uv.Offset = vector; });
    }
    else if (key == "rotate")
    {
        const std::optional<double> degrees = ParseNumber(value);
        if (!degrees.has_value())
            return PropertyEditResult::Refused;
        edit([rotation = static_cast<float>(*degrees)](UvProjection& uv) { uv.Rotation = rotation; });
    }
    else if (key == "as_one")
    {
        const std::optional<bool> on = ParseToggle(value);
        if (!on.has_value())
            return PropertyEditResult::Refused;
        TreatAsOne = *on;
    }
    else
    {
        return PropertyEditResult::Gone;
    }
    return PropertyEditResult::Applied;
}
