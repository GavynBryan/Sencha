#include "authoring/AbilityExplorers.h"

#include "authoring/AbilityElements.h"
#include "text/PathLeaf.h"

#include <array>
#include <format>

namespace
{
struct Group
{
    AbilityElementKind Kind;
    std::string_view Title;
};

constexpr std::array<Group, 3> kGroups{ {
    { AbilityElementKind::Condition, "Conditions" },
    { AbilityElementKind::Local, "Locals" },
    { AbilityElementKind::CostGroup, "Costs" },
} };

// Under a phase, its ops by the list they run in, then where it goes.
std::string_view ListTitle(std::string_view list)
{
    if (list == "enter")
        return "on entry";
    if (list == "ops")
        return "repeating";
    if (list == "hold")
        return "held";
    if (list == "exit")
        return "on exit";
    return list;
}
} // namespace

std::vector<OutlineNode> AbilityAssetNodes(std::span<const ProjectFile> abilities, std::string_view open)
{
    std::vector<OutlineNode> nodes;
    nodes.reserve(abilities.size());
    for (const ProjectFile& file : abilities)
        nodes.push_back({ .Key = file.AssetPath, .Label = std::string(PathLeaf(file.AssetPath)),
                          .Current = file.AssetPath == open });
    return nodes;
}

std::vector<OutlineNode> AbilityElementNodes(const JsonValue& data, std::string_view selected)
{
    const std::vector<AbilityElement> elements = AbilityElementsOf(data);
    std::vector<OutlineNode> nodes;
    for (const Group& group : kGroups)
    {
        const std::size_t heading = nodes.size();
        for (const AbilityElement& element : elements)
            if (element.Kind == group.Kind && element.Phase.empty() && element.List != "when")
                nodes.push_back({ .Key = element.Key, .Label = AbilityElementLabel(data, element), .Depth = 1,
                                  .Current = element.Key == selected });
        if (nodes.size() > heading)
            nodes.insert(nodes.begin() + static_cast<std::ptrdiff_t>(heading),
                         { .Key = std::format("{}{}", kAbilityGroupKey, group.Title),
                           .Label = std::string(group.Title), .StartsClosed = true });
    }
    for (const AbilityElement& phase : elements)
    {
        if (phase.Kind != AbilityElementKind::Phase)
            continue;
        nodes.push_back({ .Key = phase.Key, .Label = AbilityElementLabel(data, phase), .Current = phase.Key == selected });
        for (const AbilityElement& element : elements)
        {
            const bool inPhase = element.Phase == phase.Key
                && (element.Kind == AbilityElementKind::Op || element.Kind == AbilityElementKind::Transition);
            if (!inPhase)
                continue;
            const std::string label = element.Kind == AbilityElementKind::Op
                                        ? std::format("{} \xC2\xB7 {}", ListTitle(element.List),
                                                      AbilityElementLabel(data, element))
                                        : AbilityElementLabel(data, element);
            nodes.push_back({ .Key = element.Key, .Label = label, .Depth = 1, .Current = element.Key == selected });
        }
    }
    return nodes;
}
