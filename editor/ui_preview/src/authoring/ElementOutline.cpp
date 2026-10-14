#include "authoring/ElementOutline.h"

#include <charconv>
#include <format>
#include <vector>

namespace
{
std::string KeyOf(UiElementRef ref)
{
    return std::format("{}:{}", ref.Slot, ref.Serial);
}
} // namespace

std::string ElementLabel(const UiElementInfo& element)
{
    std::string label = element.Tag;
    if (!element.Id.empty())
        label += "#" + element.Id;
    for (const std::string& name : element.Classes)
        label += "." + name;
    return label;
}

ExplorerPage ElementOutline::Explore(std::span<const UiElementInfo> tree, UiElementRef selected,
                                     const ExplorerQuery& query)
{
    std::vector<OutlineNode> nodes;
    nodes.reserve(tree.size());
    for (const UiElementInfo& element : tree)
        nodes.push_back({ .Key = KeyOf(element.Ref),
                          .Label = ElementLabel(element),
                          .Depth = static_cast<int>(element.Depth),
                          .Current = element.Ref == selected });
    ExplorerPage page = Outline.Explore(nodes, query);
    page.Empty = tree.empty() ? "Nothing is laid out." : "No element's selector holds that.";
    return page;
}

std::optional<UiElementRef> ElementOutline::Resolve(std::span<const UiElementInfo> tree, std::string_view key)
{
    const std::size_t colon = key.find(':');
    std::uint32_t slot = 0;
    std::uint32_t serial = 0;
    if (colon == std::string_view::npos || std::from_chars(key.data(), key.data() + colon, slot).ec != std::errc{}
        || std::from_chars(key.data() + colon + 1, key.data() + key.size(), serial).ec != std::errc{})
        return std::nullopt;
    for (const UiElementInfo& element : tree)
        if (element.Ref.Slot == slot && element.Ref.Serial == serial)
            return element.Ref;
    return std::nullopt;
}

void ElementOutline::Flip(UiElementRef element)
{
    Outline.Flip(KeyOf(element));
}
