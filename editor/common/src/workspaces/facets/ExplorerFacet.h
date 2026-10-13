#pragma once

#include "icons/IconId.h"
#include "workspaces/WorkspaceKind.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Something the workspace lets a person browse and pick from: its materials,
// its prefabs, the structure of its world. The workspace says what is in one
// and what picking does; the shell decides whether it is a tray, a sheet or a
// list, and pages through it so only what shows is ever asked for.
enum class ExplorerShape : std::uint8_t
{
    Pictures, // items are recognised by their picture: a grid of tiles
    Outline,  // items are recognised by their name and place in a tree
    Files,    // the project's content files of the kinds it accepts, chosen from the shelf
};

struct WorkspaceExplorerInfo
{
    std::string Id;
    std::string Title;
    ExplorerShape Shape = ExplorerShape::Pictures;
    // The action that opens it; the workspace defines it, the shell performs it.
    std::string Action;
    // For Files: the kinds it offers, the one in use, and the asset paths it
    // must not offer (the level being edited, as a prefab of itself).
    std::vector<ContentKind> Accepts = {};
    std::string Current = {};
    std::vector<std::string> Withheld = {};
};

struct ExplorerQuery
{
    std::string Explorer;
    // Items whose label holds this, or every item when it is empty.
    std::string Filter = {};
    std::size_t First = 0;
    std::size_t Count = 0;
};

// In an outline, whether an item holds others and whether they show.
enum class ExplorerBranch : std::uint8_t
{
    Leaf,
    Closed,
    Open,
};

struct ExplorerItem
{
    // What choosing it names back to the workspace.
    std::string Key;
    std::string Label;
    // A host image picturing it, or empty.
    std::string Image = {};
    int Depth = 0;
    ExplorerBranch Branch = ExplorerBranch::Leaf;
    // The one in hand now, or one of those selected.
    bool Current = false;
    // Small switches on the item, each showing its state by its icon: an eye
    // open or shut, a lock open or fastened. Choosing one names its index.
    std::vector<IconId> Marks = {};
};

// One page of an explorer's items that match a query.
struct ExplorerPage
{
    std::vector<ExplorerItem> Items;
    // How many match, on every page together.
    std::size_t Total = 0;
    // What to say when nothing matches.
    std::string Empty = {};
    // What the explorer offers besides its items: a rescan, a new item.
    std::vector<std::string> Actions = {};
};

// Counts one more item that matches and says whether it falls on the page the
// query asks for: every match counts toward the total, only the page's are kept.
[[nodiscard]] inline bool CountOnPage(ExplorerPage& page, const ExplorerQuery& query)
{
    const std::size_t index = page.Total++;
    return index >= query.First && page.Items.size() < query.Count;
}

// Choosing an item: picking it up alone, or putting it to use at once (a
// double click, Enter); in an outline, picking everything from the current one
// to it, picking it or letting it go beside the others, opening or closing it,
// or flipping one of its marks.
enum class ExplorerChoice : std::uint8_t
{
    Pick,
    Commit,
    Extend,
    Toggle,
    Open,
    Mark,
    // Shows the item -- its ancestors opened -- and chooses nothing.
    Reveal,
};
