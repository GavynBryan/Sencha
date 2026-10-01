#pragma once

#include "workspaces/WorkspaceFacets.h"

#include <set>
#include <span>
#include <string>
#include <string_view>

// One node of an outline, in preorder: its children follow it one level
// deeper, up to the next node at its depth or shallower.
struct OutlineNode
{
    std::string Key;
    std::string Label;
    int Depth = 0;
    bool Current = false;
};

//=============================================================================
// TreeOutline
//
// An explorer's outline over nodes in preorder: every branch open until closed,
// the branches above the current node opened whenever the current node moves
// to it, and, with a filter, a node kept when it or anything under it matches,
// shown open. Pages through what shows; owns only which branches are closed.
//=============================================================================
class TreeOutline
{
public:
    [[nodiscard]] ExplorerPage Explore(std::span<const OutlineNode> nodes, const ExplorerQuery& query);
    // Opens or closes the branch `key` names.
    void Flip(std::string_view key);

private:
    std::set<std::string, std::less<>> Closed;
    std::string Revealed;
};
