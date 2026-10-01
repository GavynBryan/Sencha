#include "authoring/ElementOutline.h"

#include "text/TextFilterMatch.h"

#include <charconv>
#include <format>
#include <vector>

namespace
{
std::string KeyOf(UiElementRef ref)
{
    return std::format("{}:{}", ref.Slot, ref.Serial);
}

// One past the last element under `tree[index]`.
std::size_t SubtreeEnd(std::span<const UiElementInfo> tree, std::size_t index)
{
    std::size_t end = index + 1;
    while (end < tree.size() && tree[end].Depth > tree[index].Depth)
        ++end;
    return end;
}
// Preorder puts an element's parent at the nearest element before it that is
// shallower, and so on up.
template <typename Visit>
void ForEachAncestor(std::span<const UiElementInfo> tree, std::size_t index, Visit&& visit)
{
    std::uint32_t depth = tree[index].Depth;
    for (std::size_t up = index; up-- > 0 && depth > 0;)
        if (tree[up].Depth < depth)
        {
            depth = tree[up].Depth;
            visit(tree[up]);
        }
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
    if (selected != Revealed)
    {
        Revealed = selected;
        for (std::size_t i = 0; i < tree.size(); ++i)
            if (tree[i].Ref == selected)
                ForEachAncestor(tree, i, [this](const UiElementInfo& above) { Closed.erase(above.Ref.Slot); });
    }

    const bool filtering = !query.Filter.empty();
    std::vector<bool> keep(tree.size(), !filtering);
    if (filtering)
        for (std::size_t i = tree.size(); i-- > 0;)
        {
            if (!TextFilterMatch(ElementLabel(tree[i]), query.Filter))
                continue;
            keep[i] = true;
            ForEachAncestor(tree, i, [&](const UiElementInfo& above) {
                keep[static_cast<std::size_t>(&above - tree.data())] = true;
            });
        }

    ExplorerPage page;
    page.Empty = tree.empty() ? "Nothing is laid out." : "No element's selector holds that.";
    for (std::size_t i = 0; i < tree.size();)
    {
        const UiElementInfo& element = tree[i];
        const std::size_t end = SubtreeEnd(tree, i);
        if (!keep[i])
        {
            i = end;
            continue;
        }
        const bool leaf = end == i + 1;
        const bool open = filtering || !Closed.contains(element.Ref.Slot);
        if (CountOnPage(page, query))
            page.Items.push_back({ .Key = KeyOf(element.Ref),
                                   .Label = ElementLabel(element),
                                   .Depth = static_cast<int>(element.Depth),
                                   .Branch = leaf ? ExplorerBranch::Leaf
                                             : open ? ExplorerBranch::Open
                                                    : ExplorerBranch::Closed,
                                   .Current = element.Ref == selected });
        i = open ? i + 1 : end;
    }
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
    if (!Closed.erase(element.Slot))
        Closed.insert(element.Slot);
}
