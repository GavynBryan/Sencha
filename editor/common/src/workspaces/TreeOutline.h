#pragma once

#include "icons/IconId.h"
#include "workspaces/WorkspaceFacets.h"

#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// One node of an outline, in preorder: its children follow it one level
// deeper, up to the next node at its depth or shallower.
struct OutlineNode
{
    std::string Key;
    std::string Label;
    int Depth = 0;
    bool Current = false;
    std::vector<IconId> Marks = {};
    // A branch that reads as one thing until opened, rather than open until closed.
    bool StartsClosed = false;
};

// An explorer's outline over nodes in preorder. A branch is as it starts until
// flipped; the branches above the current nodes open whenever those move, and
// above a node revealed by key; with a filter, a node is kept when it or
// anything under it matches, shown open. Pages through what shows; owns only
// which branches depart from how they start, by key.
class TreeOutline
{
public:
    [[nodiscard]] ExplorerPage Explore(std::span<const OutlineNode> nodes, const ExplorerQuery& query);
    // Opens or closes the branch `key` names.
    void Flip(std::string_view key);
    // Opens the branches above `key` the next time the outline is explored.
    void Reveal(std::string key) { PendingReveal = std::move(key); }

private:
    void OpenAbove(std::span<const OutlineNode> nodes, std::size_t index);
    [[nodiscard]] bool IsOpen(const OutlineNode& node) const;

    std::set<std::string, std::less<>> Flipped;
    std::vector<std::string> Revealed;
    std::string PendingReveal;
};
