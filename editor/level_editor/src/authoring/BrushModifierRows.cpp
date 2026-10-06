#include "authoring/BrushModifierRows.h"

#include "properties/SettingProperties.h"

#include <optional>
#include <utility>

std::string_view ModifierAxisLabel(LocalAxis axis)
{
    return kModifierAxes[static_cast<std::size_t>(axis)];
}

PropertyNode AdvancedModifierField(PropertyNode node)
{
    node.Descriptor.Advanced = true;
    return node;
}

PropertyNode ModifierDistance(std::string key, std::string label, float value, std::string tooltip)
{
    return NumberProperty(std::move(key), std::move(label), value, 0.0, -1.0, 0.05, std::move(tooltip));
}

bool SetModifierAxis(LocalAxis& target, std::string_view value)
{
    const std::optional<std::size_t> choice = ParseChoice(value, kModifierAxes);
    if (choice.has_value())
        target = static_cast<LocalAxis>(*choice);
    return choice.has_value();
}

bool SetModifierNumber(float& target, std::string_view value)
{
    const std::optional<double> parsed = ParseNumber(value);
    if (parsed.has_value())
        target = static_cast<float>(*parsed);
    return parsed.has_value();
}

bool SetModifierVector(Vec3d& target, std::string_view value)
{
    const std::optional<std::vector<double>> parsed = ParseVector(value, 3);
    if (parsed.has_value())
        target = Vec3d{ static_cast<float>((*parsed)[0]), static_cast<float>((*parsed)[1]),
                        static_cast<float>((*parsed)[2]) };
    return parsed.has_value();
}
