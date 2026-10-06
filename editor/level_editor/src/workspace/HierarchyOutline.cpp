#include "workspace/HierarchyOutline.h"

#include "workspace/EditorWorkspace.h"
#include "workspace/PartitionOutline.h"
#include "workspace/WorldExplorer.h"

#include <zone/WorldPartitionIds.h>

#include <algorithm>
#include <limits>

namespace
{
constexpr std::string_view kEntityPrefix = "entity:";
constexpr std::size_t kEverything = std::numeric_limits<std::size_t>::max();
} // namespace

WorkspaceExplorerInfo HierarchyOutline::Info()
{
    return { .Id = std::string(kId), .Title = "Hierarchy", .Shape = ExplorerShape::Outline,
             .Action = "explorer.hierarchy" };
}

std::string HierarchyOutline::EntityKey(std::string_view entityKey)
{
    return std::string(kEntityPrefix) + std::string(entityKey);
}

ExplorerPage HierarchyOutline::Explore(const ExplorerQuery& query)
{
    ExplorerPage entities = Entities.Explore({ .Explorer = query.Explorer, .Filter = query.Filter, .First = 0,
                                               .Count = kEverything });
    for (ExplorerItem& item : entities.Items)
        item.Key = EntityKey(item.Key);

    std::vector<ExplorerItem> rows;
    ExplorerPage page;
    WorldDocument& world = Workspace.World;
    if (!world.IsWorld())
    {
        rows = std::move(entities.Items);
        page.Actions = std::move(entities.Actions);
        page.Empty = std::move(entities.Empty);
    }
    else
    {
        ExplorerPage partition = Partition.Explore({ .Explorer = query.Explorer, .Filter = query.Filter, .First = 0,
                                                     .Count = kEverything });
        rows = std::move(partition.Items);
        // The entities sit under what is being edited.
        const std::string anchor =
            world.IsWorldSceneFocused() ? std::string("world") : "zone:" + ZoneIdToString(world.FocusZone());
        auto under = std::ranges::find(rows, anchor, &ExplorerItem::Key);
        const int depth = under != rows.end() ? under->Depth + 1 : 0;
        if (under != rows.end() && !entities.Items.empty())
            under->Branch = ExplorerBranch::Open;
        for (ExplorerItem& item : entities.Items)
            item.Depth += depth;
        const auto at = under != rows.end() ? under + 1 : rows.end();
        rows.insert(at, std::make_move_iterator(entities.Items.begin()), std::make_move_iterator(entities.Items.end()));
        page.Actions = std::move(entities.Actions);
        page.Actions.insert(page.Actions.end(), partition.Actions.begin(), partition.Actions.end());
        page.Empty = std::move(partition.Empty);
    }
    page.Total = rows.size();
    for (std::size_t i = query.First; i < rows.size() && page.Items.size() < query.Count; ++i)
        page.Items.push_back(std::move(rows[i]));
    return page;
}

void HierarchyOutline::Choose(std::string_view key, ExplorerChoice choice, std::size_t mark)
{
    if (key.starts_with(kEntityPrefix))
        Entities.Choose(key.substr(kEntityPrefix.size()), choice, mark);
    else
        Partition.Choose(key, choice, mark);
}
