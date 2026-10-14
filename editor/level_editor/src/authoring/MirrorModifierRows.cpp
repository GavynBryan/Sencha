#include "authoring/BrushModifierRows.h"

#include "properties/SettingProperties.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <optional>

namespace
{
constexpr std::array<std::string_view, 3> kPlaneSources = { "Origin", "Bounds Center", "Custom plane" };
} // namespace

ModifierKindText ModifierText(const MirrorModifier&)
{
    return { .Label = "Mirror",
             .Description = "Reflects what the stack has made so far across a plane through the brush" };
}

std::string ModifierSummary(const MirrorModifier& mirror)
{
    std::string text(ModifierAxisLabel(mirror.Axis));
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

std::vector<PropertyNode> ModifierFields(const std::string& prefix, const MirrorModifier& mirror)
{
    std::vector<PropertyNode> fields;
    if (mirror.Source != MirrorPlaneSource::Custom)
    {
        fields.push_back(ChoiceProperty(prefix + ".axis", "Axis", kModifierAxes, static_cast<std::size_t>(mirror.Axis)));
        fields.push_back(ModifierDistance(prefix + ".offset", "Offset", mirror.Offset,
                                          "Distance of the mirror plane from the brush origin along the axis"));
    }
    fields.push_back(AdvancedModifierField(
        ChoiceProperty(prefix + ".plane", "Plane", kPlaneSources, static_cast<std::size_t>(mirror.Source))));
    if (mirror.Source == MirrorPlaneSource::Custom)
    {
        const Vec3d n = mirror.CustomPlane.Normal;
        const std::array<double, 3> normal{ n.X, n.Y, n.Z };
        fields.push_back(AdvancedModifierField(VectorProperty(prefix + ".normal", "Normal", normal)));
        fields.push_back(
            AdvancedModifierField(ModifierDistance(prefix + ".distance", "Distance", -mirror.CustomPlane.D)));
    }
    return fields;
}

bool SetModifierField(MirrorModifier& mirror, std::string_view field, std::string_view value)
{
    if (field == "axis")
        return SetModifierAxis(mirror.Axis, value);
    if (field == "offset")
        return SetModifierNumber(mirror.Offset, value);
    if (field == "plane")
    {
        const std::optional<std::size_t> choice = ParseChoice(value, kPlaneSources);
        if (choice.has_value())
            mirror.Source = static_cast<MirrorPlaneSource>(*choice);
        return choice.has_value();
    }
    if (field == "normal")
        return SetModifierVector(mirror.CustomPlane.Normal, value);
    if (field == "distance")
    {
        float distance = -mirror.CustomPlane.D;
        if (!SetModifierNumber(distance, value))
            return false;
        mirror.CustomPlane.D = -distance;
        return true;
    }
    return false;
}
