#include "BrushModifierEditorAdapter.h"

#include "brush/BrushEvaluation.h"
#include "brush/BrushModifier.h"
#include "commands/CommandStack.h"
#include "document/BrushComponents.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/commands/ValueCommand.h"
#include "properties/SettingProperties.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

namespace
{
    // Display names, indexed like BrushModifierKinds(): the one place a kind
    // is named for a person.
    constexpr std::array<std::string_view, 2> kKindLabels = { "Mirror", "Array" };
    static_assert(kKindLabels.size() == std::variant_size_v<decltype(BrushModifier::Params)>);

    const char* AxisLabel(LocalAxis axis)
    {
        return axis == LocalAxis::Y ? "Y" : axis == LocalAxis::Z ? "Z" : "X";
    }

    // The header's one-line reading of a row, so a collapsed stack still says
    // what it does: "X · Origin", "X · ×5 · gap 16".
    struct Summary
    {
        std::string operator()(const MirrorModifier& mirror) const
        {
            std::string text = AxisLabel(mirror.Axis);
            switch (mirror.Source)
            {
            case MirrorPlaneSource::BoundsCenter: text += " \xC2\xB7 Bounds Center"; break;
            case MirrorPlaneSource::Custom:       text += " \xC2\xB7 Custom plane"; break;
            case MirrorPlaneSource::Origin:
            default:                              text += " \xC2\xB7 Origin"; break;
            }
            if (mirror.Source != MirrorPlaneSource::Custom && std::abs(mirror.Offset) > 1e-6f)
            {
                char buf[32];
                std::snprintf(buf, sizeof(buf), " %+.3g", mirror.Offset);
                text += buf;
            }
            return text;
        }

        std::string operator()(const ArrayModifier& array) const
        {
            char buf[96];
            if (array.Placement == ArrayPlacement::ConstantOffset)
                std::snprintf(buf, sizeof(buf), "%s%s \xC2\xB7 \xC3\x97%d \xC2\xB7 offset (%.3g, %.3g, %.3g)",
                              array.Reverse ? "-" : "", AxisLabel(array.Axis), array.Count,
                              array.Offset.X, array.Offset.Y, array.Offset.Z);
            else
                std::snprintf(buf, sizeof(buf), "%s%s \xC2\xB7 \xC3\x97%d \xC2\xB7 gap %.3g",
                              array.Reverse ? "-" : "", AxisLabel(array.Axis), array.Count,
                              array.Spacing);
            return buf;
        }
    };

    constexpr std::array<std::string_view, 3> kAxes = { "X", "Y", "Z" };
    constexpr std::array<std::string_view, 3> kPlaneSources = { "Origin", "Bounds Center", "Custom plane" };
    constexpr std::array<std::string_view, 2> kPlacements = { "Relative to bounds", "Constant offset" };

    // "m2.offset" -> modifier 2, field "offset"; "m2" -> modifier 2, no field.
    struct RowKey
    {
        std::size_t Index = 0;
        std::string_view Field;
    };

    std::optional<RowKey> ParseRowKey(std::string_view key)
    {
        if (!key.starts_with('m'))
            return std::nullopt;
        key.remove_prefix(1);
        const std::size_t dot = key.find('.');
        const std::string_view digits = key.substr(0, dot);
        RowKey row;
        if (digits.empty() || std::from_chars(digits.data(), digits.data() + digits.size(), row.Index).ec != std::errc{})
            return std::nullopt;
        if (dot != std::string_view::npos)
            row.Field = key.substr(dot + 1);
        return row;
    }

    PropertyNode Advanced(PropertyNode node)
    {
        node.Descriptor.Advanced = true;
        return node;
    }

    PropertyNode Distance(std::string key, std::string label, float value, std::string tooltip = {})
    {
        return NumberProperty(std::move(key), std::move(label), value, 0.0, -1.0, 0.05, std::move(tooltip));
    }

    std::vector<PropertyNode> ModifierFields(const std::string& prefix, const BrushModifier& modifier)
    {
        std::vector<PropertyNode> fields;
        fields.push_back(ToggleProperty(prefix + ".enabled", "Enabled", modifier.Enabled));
        if (const auto* mirror = std::get_if<MirrorModifier>(&modifier.Params))
        {
            if (mirror->Source != MirrorPlaneSource::Custom)
            {
                fields.push_back(ChoiceProperty(prefix + ".axis", "Axis", kAxes, static_cast<std::size_t>(mirror->Axis)));
                fields.push_back(Distance(prefix + ".offset", "Offset", mirror->Offset,
                                          "Distance of the mirror plane from the brush origin along the axis"));
            }
            fields.push_back(Advanced(ChoiceProperty(prefix + ".plane", "Plane", kPlaneSources,
                                                     static_cast<std::size_t>(mirror->Source))));
            if (mirror->Source == MirrorPlaneSource::Custom)
            {
                const Vec3d n = mirror->CustomPlane.Normal;
                const std::array<double, 3> normal{ n.X, n.Y, n.Z };
                fields.push_back(Advanced(VectorProperty(prefix + ".normal", "Normal", normal)));
                fields.push_back(Advanced(Distance(prefix + ".distance", "Distance", -mirror->CustomPlane.D)));
            }
        }
        else if (const auto* array = std::get_if<ArrayModifier>(&modifier.Params))
        {
            fields.push_back(ChoiceProperty(prefix + ".axis", "Axis", kAxes, static_cast<std::size_t>(array->Axis)));
            fields.push_back(NumberProperty(prefix + ".count", "Count", array->Count, 0.0, -1.0, 1.0,
                                            "Copies, the source included"));
            if (array->Placement == ArrayPlacement::RelativeToBounds)
                fields.push_back(Distance(prefix + ".gap", "Gap", array->Spacing,
                                          "Empty space between copies; 0 makes them touch"));
            fields.push_back(Advanced(ToggleProperty(prefix + ".reverse", "Reverse direction", array->Reverse)));
            fields.push_back(Advanced(ChoiceProperty(prefix + ".placement", "Placement", kPlacements,
                                                     static_cast<std::size_t>(array->Placement))));
            if (array->Placement == ArrayPlacement::ConstantOffset)
            {
                const std::array<double, 3> offset{ array->Offset.X, array->Offset.Y, array->Offset.Z };
                fields.push_back(Advanced(VectorProperty(prefix + ".step", "Offset", offset)));
            }
        }
        return fields;
    }

    // `value` written into one field of `modifier`; false when it is not a
    // value that field can hold.
    bool SetField(BrushModifier& modifier, std::string_view field, std::string_view value)
    {
        if (field == "enabled")
        {
            const std::optional<bool> on = ParseToggle(value);
            if (on.has_value())
                modifier.Enabled = *on;
            return on.has_value();
        }
        const auto axis = [&](LocalAxis& target) {
            const std::optional<std::size_t> choice = ParseChoice(value, kAxes);
            if (choice.has_value())
                target = static_cast<LocalAxis>(*choice);
            return choice.has_value();
        };
        const auto number = [&](float& target) {
            const std::optional<double> parsed = ParseNumber(value);
            if (parsed.has_value())
                target = static_cast<float>(*parsed);
            return parsed.has_value();
        };
        const auto vector = [&](Vec3d& target) {
            const std::optional<std::vector<double>> parsed = ParseVector(value, 3);
            if (parsed.has_value())
                target = Vec3d{ static_cast<float>((*parsed)[0]), static_cast<float>((*parsed)[1]),
                                static_cast<float>((*parsed)[2]) };
            return parsed.has_value();
        };
        if (auto* mirror = std::get_if<MirrorModifier>(&modifier.Params))
        {
            if (field == "axis")
                return axis(mirror->Axis);
            if (field == "offset")
                return number(mirror->Offset);
            if (field == "plane")
            {
                const std::optional<std::size_t> choice = ParseChoice(value, kPlaneSources);
                if (choice.has_value())
                    mirror->Source = static_cast<MirrorPlaneSource>(*choice);
                return choice.has_value();
            }
            if (field == "normal")
                return vector(mirror->CustomPlane.Normal);
            if (field == "distance")
            {
                float distance = -mirror->CustomPlane.D;
                if (!number(distance))
                    return false;
                mirror->CustomPlane.D = -distance;
                return true;
            }
        }
        else if (auto* array = std::get_if<ArrayModifier>(&modifier.Params))
        {
            if (field == "axis")
                return axis(array->Axis);
            if (field == "count")
            {
                const std::optional<double> count =
                    ParseNumber(value, 1.0, static_cast<double>(BrushEvaluationPolicy::kHardPieceLimit));
                if (count.has_value())
                    array->Count = static_cast<int>(std::lround(*count));
                return count.has_value();
            }
            if (field == "gap")
                return number(array->Spacing);
            if (field == "reverse")
            {
                const std::optional<bool> on = ParseToggle(value);
                if (on.has_value())
                    array->Reverse = *on;
                return on.has_value();
            }
            if (field == "placement")
            {
                const std::optional<std::size_t> choice = ParseChoice(value, kPlacements);
                if (choice.has_value())
                    array->Placement = static_cast<ArrayPlacement>(*choice);
                return choice.has_value();
            }
            if (field == "step")
                return vector(array->Offset);
        }
        return false;
    }

    class BrushModifierEditorAdapter final : public IEditorComponentAdapter
    {
    public:
        [[nodiscard]] ComponentTypeId Type() const override
        {
            return ResolveComponentTypeId<BrushComponent>();
        }

        bool AuthorsInspectorRows() const override { return true; }

        std::vector<PropertyNode> InspectorNodes(const EditorComponentInspectorContext& context) const override
        {
            const BrushComponent* brush = context.Scene.TryGetBrush(context.Entity);
            const BrushModifierStack* stack = context.Scene.TryGetBrushModifiers(context.Entity);
            if (brush == nullptr || stack == nullptr)
                return {};

            std::optional<std::uint32_t> failed;
            bool overHardLimit = false;
            if (const BrushEvaluated* evaluated = context.Scene.TryGetBrushPieces(context.Entity);
                evaluated != nullptr && evaluated->Status != BrushEvaluationStatus::Ok)
            {
                failed = evaluated->FailedModifier;
                // Projected from the stack's factors, not from a second
                // evaluation under the cook policy.
                overHardLimit = BrushProjectedPieceCount(*stack) > BrushEvaluationPolicy::kHardPieceLimit;
            }

            PropertyNode modifiers;
            modifiers.Key = "stack";
            modifiers.Descriptor.Kind = PropertyKind::Group;
            modifiers.Descriptor.Label = "Modifiers";
            modifiers.Descriptor.Verbs.push_back({ .Id = "add", .Label = "Add Modifier",
                                                   .Choices = { kKindLabels.begin(), kKindLabels.end() } });
            for (std::size_t i = 0; i < stack->size(); ++i)
            {
                const BrushModifier& modifier = (*stack)[i];
                const std::string prefix = "m" + std::to_string(i);
                PropertyNode row;
                row.Key = prefix;
                row.Descriptor.Kind = PropertyKind::Group;
                row.Descriptor.Label = std::string(kKindLabels[modifier.Params.index()]) + "  "
                                     + std::visit(Summary{}, modifier.Params);
                if (i > 0)
                    row.Descriptor.Verbs.push_back({ .Id = "up", .Label = "Up" });
                if (i + 1 < stack->size())
                    row.Descriptor.Verbs.push_back({ .Id = "down", .Label = "Down" });
                row.Descriptor.Verbs.push_back({ .Id = "remove", .Label = "Remove" });
                row.Children = ModifierFields(prefix, modifier);
                if (failed == i)
                    row.Children.push_back(NoteProperty(prefix + ".failed", "Not shown",
                                                overHardLimit ? "Exceeds the piece limit: the level will not cook."
                                                              : "Exceeds the preview piece budget."));
                modifiers.Children.push_back(std::move(row));
            }
            return { NoteProperty("brush", "Brush", std::to_string(brush->Id.Value)), std::move(modifiers) };
        }

        PropertyEditResult SetInspectorValue(EditorComponentInspectorContext& context, std::string_view key,
                                             std::string_view value) const override
        {
            const BrushModifierStack* stored = context.Scene.TryGetBrushModifiers(context.Entity);
            const std::optional<RowKey> row = ParseRowKey(key);
            if (stored == nullptr || !row.has_value() || row->Index >= stored->size())
                return PropertyEditResult::Gone;
            BrushModifierStack edited = *stored;
            if (!SetField(edited[row->Index], row->Field, value))
                return PropertyEditResult::Refused;
            return Commit(context, *stored, std::move(edited));
        }

        PropertyEditResult ActInspector(EditorComponentInspectorContext& context, std::string_view key,
                                        std::string_view verb, std::string_view choice) const override
        {
            const BrushModifierStack* stored = context.Scene.TryGetBrushModifiers(context.Entity);
            if (stored == nullptr)
                return PropertyEditResult::Gone;
            BrushModifierStack edited = *stored;
            if (key == "stack" && verb == "add")
            {
                const auto kind = std::ranges::find(kKindLabels, choice);
                if (kind == kKindLabels.end())
                    return PropertyEditResult::Refused;
                edited.push_back(BrushModifierKinds()[static_cast<std::size_t>(kind - kKindLabels.begin())].MakeDefault());
                return Commit(context, *stored, std::move(edited));
            }
            const std::optional<RowKey> row = ParseRowKey(key);
            if (!row.has_value() || row->Index >= edited.size())
                return PropertyEditResult::Gone;
            const auto at = static_cast<std::ptrdiff_t>(row->Index);
            if (verb == "remove")
                edited.erase(edited.begin() + at);
            else if (verb == "up" && row->Index > 0)
                std::swap(edited[row->Index], edited[row->Index - 1]);
            else if (verb == "down" && row->Index + 1 < edited.size())
                std::swap(edited[row->Index], edited[row->Index + 1]);
            else
                return PropertyEditResult::Refused;
            return Commit(context, *stored, std::move(edited));
        }

    private:
        static PropertyEditResult Commit(EditorComponentInspectorContext& context, BrushModifierStack before,
                                         BrushModifierStack after)
        {
            context.Commands.Execute(MakeEditBrushModifiersCommand(
                context.Entity, std::move(before), std::move(after), context.Scene, context.Document));
            return PropertyEditResult::Applied;
        }
    };
}

std::unique_ptr<IEditorComponentAdapter> MakeBrushModifierEditorAdapter()
{
    return std::make_unique<BrushModifierEditorAdapter>();
}
