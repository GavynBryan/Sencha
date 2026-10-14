#include "authoring/BrushModifierRows.h"

#include "brush/BrushEvaluation.h"
#include "properties/SettingProperties.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <optional>

namespace
{
constexpr std::array<std::string_view, 2> kPlacements = { "Relative to bounds", "Constant offset" };
} // namespace

ModifierKindText ModifierText(const ArrayModifier&)
{
    return { .Label = "Array", .Description = "Repeats what the stack has made so far along an axis" };
}

std::string ModifierSummary(const ArrayModifier& array)
{
    char buf[96];
    const std::string axis(ModifierAxisLabel(array.Axis));
    if (array.Placement == ArrayPlacement::ConstantOffset)
        std::snprintf(buf, sizeof(buf), "%s%s \xC2\xB7 \xC3\x97%d \xC2\xB7 offset (%.3g, %.3g, %.3g)",
                      array.Reverse ? "-" : "", axis.c_str(), array.Count, array.Offset.X, array.Offset.Y,
                      array.Offset.Z);
    else
        std::snprintf(buf, sizeof(buf), "%s%s \xC2\xB7 \xC3\x97%d \xC2\xB7 gap %.3g", array.Reverse ? "-" : "",
                      axis.c_str(), array.Count, array.Spacing);
    return buf;
}

std::vector<PropertyNode> ModifierFields(const std::string& prefix, const ArrayModifier& array)
{
    std::vector<PropertyNode> fields;
    fields.push_back(ChoiceProperty(prefix + ".axis", "Axis", kModifierAxes, static_cast<std::size_t>(array.Axis)));
    fields.push_back(NumberProperty(prefix + ".count", "Count", array.Count, 0.0, -1.0, 1.0,
                                    "Copies, the source included"));
    if (array.Placement == ArrayPlacement::RelativeToBounds)
        fields.push_back(ModifierDistance(prefix + ".gap", "Gap", array.Spacing,
                                          "Empty space between copies; 0 makes them touch"));
    fields.push_back(AdvancedModifierField(ToggleProperty(prefix + ".reverse", "Reverse direction", array.Reverse)));
    fields.push_back(AdvancedModifierField(
        ChoiceProperty(prefix + ".placement", "Placement", kPlacements, static_cast<std::size_t>(array.Placement))));
    if (array.Placement == ArrayPlacement::ConstantOffset)
    {
        const std::array<double, 3> offset{ array.Offset.X, array.Offset.Y, array.Offset.Z };
        fields.push_back(AdvancedModifierField(VectorProperty(prefix + ".step", "Offset", offset)));
    }
    return fields;
}

bool SetModifierField(ArrayModifier& array, std::string_view field, std::string_view value)
{
    if (field == "axis")
        return SetModifierAxis(array.Axis, value);
    if (field == "count")
    {
        const std::optional<double> count =
            ParseNumber(value, 1.0, static_cast<double>(BrushEvaluationPolicy::kHardPieceLimit));
        if (count.has_value())
            array.Count = static_cast<int>(std::lround(*count));
        return count.has_value();
    }
    if (field == "gap")
        return SetModifierNumber(array.Spacing, value);
    if (field == "reverse")
    {
        const std::optional<bool> on = ParseToggle(value);
        if (on.has_value())
            array.Reverse = *on;
        return on.has_value();
    }
    if (field == "placement")
    {
        const std::optional<std::size_t> choice = ParseChoice(value, kPlacements);
        if (choice.has_value())
            array.Placement = static_cast<ArrayPlacement>(*choice);
        return choice.has_value();
    }
    if (field == "step")
        return SetModifierVector(array.Offset, value);
    return false;
}
