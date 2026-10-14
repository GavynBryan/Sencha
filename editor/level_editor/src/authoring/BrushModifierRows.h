#pragma once

#include "brush/ArrayModifier.h"
#include "brush/MirrorModifier.h"
#include "properties/PropertyNode.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

// How each modifier kind reads and is edited in a sheet: what it is called and
// what it does, its one-line reading for a closed row, its fields keyed under
// `prefix`, and one field set from text. One overload set per kind, in the
// kind's own file and reached by std::visit, so a kind without one does not
// compile.
struct ModifierKindText
{
    std::string_view Label;
    std::string_view Description;
};

[[nodiscard]] ModifierKindText ModifierText(const MirrorModifier&);
[[nodiscard]] std::string ModifierSummary(const MirrorModifier& mirror);
[[nodiscard]] std::vector<PropertyNode> ModifierFields(const std::string& prefix, const MirrorModifier& mirror);
[[nodiscard]] bool SetModifierField(MirrorModifier& mirror, std::string_view field, std::string_view value);

[[nodiscard]] ModifierKindText ModifierText(const ArrayModifier&);
[[nodiscard]] std::string ModifierSummary(const ArrayModifier& array);
[[nodiscard]] std::vector<PropertyNode> ModifierFields(const std::string& prefix, const ArrayModifier& array);
[[nodiscard]] bool SetModifierField(ArrayModifier& array, std::string_view field, std::string_view value);

// What every kind's rows are made of.
inline constexpr std::array<std::string_view, 3> kModifierAxes = { "X", "Y", "Z" };
[[nodiscard]] std::string_view ModifierAxisLabel(LocalAxis axis);
[[nodiscard]] PropertyNode AdvancedModifierField(PropertyNode node);
[[nodiscard]] PropertyNode ModifierDistance(std::string key, std::string label, float value, std::string tooltip = {});
[[nodiscard]] bool SetModifierAxis(LocalAxis& target, std::string_view value);
[[nodiscard]] bool SetModifierNumber(float& target, std::string_view value);
[[nodiscard]] bool SetModifierVector(Vec3d& target, std::string_view value);
