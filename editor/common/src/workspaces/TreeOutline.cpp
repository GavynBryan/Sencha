#include "workspaces/TreeOutline.h"

#include "text/TextFilterMatch.h"

#include <algorithm>

namespace
{
// One past the last node under `nodes[index]`.
std::size_t SubtreeEnd(std::span<const OutlineNode> nodes, std::size_t index)
{
    std::size_t end = index + 1;
    while (end < nodes.size() && nodes[end].Depth > nodes[index].Depth)
        ++end;
    return end;
}

// Preorder puts a node's parent at the nearest node before it that is
// shallower, and so on up.
template <typename Visit>
void ForEachAncestor(std::span<const OutlineNode> nodes, std::size_t index, Visit&& visit)
{
    int depth = nodes[index].Depth;
    for (std::size_t up = index; up-- > 0 && depth > 0;)
        if (nodes[up].Depth < depth)
        {
            depth = nodes[up].Depth;
            visit(up);
        }
}
} // namespace

bool TreeOutline::IsOpen(const OutlineNode& node) const
{
    return node.StartsClosed == Flipped.contains(node.Key);
}

void TreeOutline::OpenAbove(std::span<const OutlineNode> nodes, std::size_t index)
{
    ForEachAncestor(nodes, index, [&](std::size_t up) {
        const OutlineNode& ancestor = nodes[up];
        if (ancestor.StartsClosed)
            Flipped.insert(ancestor.Key);
        else if (const auto flipped = Flipped.find(ancestor.Key); flipped != Flipped.end())
            Flipped.erase(flipped);
    });
}

ExplorerPage TreeOutline::Explore(std::span<const OutlineNode> nodes, const ExplorerQuery& query)
{
    std::vector<std::string> current;
    for (const OutlineNode& node : nodes)
        if (node.Current)
            current.push_back(node.Key);
    const bool moved = current != Revealed;
    for (std::size_t i = 0; i < nodes.size(); ++i)
        if ((moved && nodes[i].Current) || (!PendingReveal.empty() && nodes[i].Key == PendingReveal))
            OpenAbove(nodes, i);
    Revealed = std::move(current);
    PendingReveal.clear();

    const bool filtering = !query.Filter.empty();
    std::vector<bool> keep(nodes.size(), !filtering);
    if (filtering)
        for (std::size_t i = 0; i < nodes.size(); ++i)
            if (TextFilterMatch(nodes[i].Label, query.Filter))
            {
                keep[i] = true;
                ForEachAncestor(nodes, i, [&](std::size_t up) { keep[up] = true; });
            }

    ExplorerPage page;
    for (std::size_t i = 0; i < nodes.size();)
    {
        const OutlineNode& node = nodes[i];
        const std::size_t end = SubtreeEnd(nodes, i);
        if (!keep[i])
        {
            i = end;
            continue;
        }
        const bool leaf = end == i + 1;
        const bool open = filtering || IsOpen(node);
        if (CountOnPage(page, query))
            page.Items.push_back({ .Key = node.Key,
                                   .Label = node.Label,
                                   .Depth = node.Depth,
                                   .Branch = leaf ? ExplorerBranch::Leaf
                                             : open ? ExplorerBranch::Open
                                                    : ExplorerBranch::Closed,
                                   .Current = node.Current,
                                   .Marks = node.Marks });
        i = open ? i + 1 : end;
    }
    return page;
}

void TreeOutline::Flip(std::string_view key)
{
    if (const auto flipped = Flipped.find(key); flipped != Flipped.end())
        Flipped.erase(flipped);
    else
        Flipped.emplace(key);
}
