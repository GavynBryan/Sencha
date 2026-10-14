#pragma once

#include "actions/ActionCatalog.h"
#include "workspaces/facets/ExplorerFacet.h"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

class ActionContext;

// One explorer a workspace offers: what it is called and opened by, what it
// lists, and what choosing in it does.
struct ExplorerRow
{
    std::string Id;
    std::string Title;
    IconId Icon = IconId::None;
    std::vector<KeyChord> Chords = {};
    std::function<ExplorerPage(const ExplorerQuery&)> Explore = {};
    std::function<void(std::string_view key, ExplorerChoice choice, std::size_t mark)> Choose = {};
    ExplorerShape Shape = ExplorerShape::Outline;
    // What it is as of now beyond the above: for Files, the kinds it offers,
    // the one in use and the ones withheld.
    std::function<void(WorkspaceExplorerInfo&)> Describe = {};
};

// A workspace's explorers as one table: each row's `explorer.<id>` action,
// its entry among the workspace's explorers, and its paging and choosing. A
// workspace adds a row; the shell finds an explorer by its id.
class ExplorerTable
{
public:
    void Offer(ActionCatalog& catalog, ActionContext& context, ExplorerRow row);

    [[nodiscard]] std::vector<WorkspaceExplorerInfo> Infos() const;
    [[nodiscard]] ExplorerPage Explore(const ExplorerQuery& query) const;
    void Choose(std::string_view explorer, std::string_view key, ExplorerChoice choice, std::size_t mark) const;

    [[nodiscard]] static std::string ActionOf(std::string_view explorer) { return "explorer." + std::string(explorer); }

private:
    [[nodiscard]] const ExplorerRow* Find(std::string_view explorer) const;

    std::vector<ExplorerRow> Rows;
};
