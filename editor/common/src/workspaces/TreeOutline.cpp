#include "workspaces/TreeOutline.h"

#include "text/TextFilterMatch.h"

#include <vector>

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

ExplorerPage TreeOutline::Explore(std::span<const OutlineNode> nodes, const ExplorerQuery& query)
{
    for (std::size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].Current)
        {
            if (nodes[i].Key != Revealed)
            {
                Revealed = nodes[i].Key;
                ForEachAncestor(nodes, i, [&](std::size_t up) {
                    if (const auto closed = Closed.find(nodes[up].Key); closed != Closed.end())
                        Closed.erase(closed);
                });
            }
            break;
        }

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
        const bool open = filtering || !Closed.contains(node.Key);
        if (CountOnPage(page, query))
            page.Items.push_back({ .Key = node.Key,
                                   .Label = node.Label,
                                   .Depth = node.Depth,
                                   .Branch = leaf ? ExplorerBranch::Leaf
                                             : open ? ExplorerBranch::Open
                                                    : ExplorerBranch::Closed,
                                   .Current = node.Current });
        i = open ? i + 1 : end;
    }
    return page;
}

void TreeOutline::Flip(std::string_view key)
{
    if (const auto closed = Closed.find(key); closed != Closed.end())
        Closed.erase(closed);
    else
        Closed.emplace(key);
}
