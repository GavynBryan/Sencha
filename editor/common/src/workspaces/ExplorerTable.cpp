#include "workspaces/ExplorerTable.h"

#include "actions/ActionOffer.h"

#include <algorithm>
#include <utility>

void ExplorerTable::Offer(ActionCatalog& catalog, ActionContext& context, ExplorerRow row)
{
    // Opening an explorer is the shell's; the workspace only names it.
    OfferAction(catalog, context,
                { .Id = ActionOf(row.Id), .Label = row.Title, .Category = "Explore", .Icon = row.Icon,
                  .DefaultChords = row.Chords },
                {});
    Rows.push_back(std::move(row));
}

std::vector<WorkspaceExplorerInfo> ExplorerTable::Infos() const
{
    std::vector<WorkspaceExplorerInfo> infos;
    infos.reserve(Rows.size());
    for (const ExplorerRow& row : Rows)
        infos.push_back({ .Id = row.Id, .Title = row.Title, .Shape = row.Shape, .Action = ActionOf(row.Id) });
    return infos;
}

ExplorerPage ExplorerTable::Explore(const ExplorerQuery& query) const
{
    const ExplorerRow* row = Find(query.Explorer);
    return row != nullptr && row->Explore ? row->Explore(query) : ExplorerPage{};
}

void ExplorerTable::Choose(std::string_view explorer, std::string_view key, ExplorerChoice choice,
                           std::size_t mark) const
{
    if (const ExplorerRow* row = Find(explorer); row != nullptr && row->Choose)
        row->Choose(key, choice, mark);
}

const ExplorerRow* ExplorerTable::Find(std::string_view explorer) const
{
    const auto found = std::ranges::find(Rows, explorer, &ExplorerRow::Id);
    return found != Rows.end() ? &*found : nullptr;
}
