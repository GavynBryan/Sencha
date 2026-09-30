#include "workspace/FaceUvSource.h"

#include "actions/ActionOffer.h"
#include "brush/BrushMesh.h"
#include "meshedit/FaceMaterialEdits.h"
#include "properties/SettingProperties.h"
#include "workspace/BrushManipulationSink.h"
#include "workspace/EditorWorkspace.h"

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
        && RepresentativeFaceUv(*Workspace.Interaction.Sink, Workspace.Selection).has_value();
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
    const auto offer = [&](std::string id, std::string label, std::function<void()> invoke) {
        OfferAction(catalog, context, { .Id = std::move(id), .Label = std::move(label), .Category = "Texture" },
                    { .Invoke = std::move(invoke), .Enabled = [this] { return HasFaces(); } });
    };
    offer("uv.fit", "Fit Texture", [this] { Justify(true); });
    offer("uv.center", "Center Texture", [this] { Justify(false); });
    offer("uv.reset_axes", "Reset Texture Axes", [this] {
        if (Workspace.Interaction.Sink != nullptr)
            Realign(*Workspace.Interaction.Sink, Workspace.Selection, Workspace.Commands, std::nullopt);
    });
}

std::vector<PropertyNode> FaceUvSource::Nodes() const
{
    if (Workspace.Interaction.Sink == nullptr)
        return {};
    const std::optional<UvProjection> uv = RepresentativeFaceUv(*Workspace.Interaction.Sink, Workspace.Selection);
    if (!uv.has_value())
        return {};
    const std::array<double, 2> scale{ uv->Scale.X, uv->Scale.Y };
    const std::array<double, 2> shift{ uv->Offset.X, uv->Offset.Y };
    return {
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

bool FaceUvSource::BeginEdit(std::string_view key)
{
    return !ValueOf(key).empty();
}

PropertyEditResult FaceUvSource::PreviewEdit(std::string_view, std::string_view)
{
    // A face edit is a mesh command; there is nothing to show before it is one.
    return PropertyEditResult::Unchanged;
}

PropertyEditResult FaceUvSource::CommitEdit(std::string_view key, std::string_view value)
{
    return Set(key, value);
}

void FaceUvSource::CancelEdit(std::string_view)
{
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
    if (key == "align")
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
