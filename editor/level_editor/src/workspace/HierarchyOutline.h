#pragma once

#include "workspaces/TreeOutline.h"
#include "workspaces/ExplorerTable.h"

#include <string_view>

class EditorWorkspace;
class PartitionOutline;
class WorldExplorer;

// The level as one outline: the world, its graphs and their zones, with the
// entities of what is being edited -- the focused zone, or the world scene --
// under it. A level that is not a world is its entities alone. The partition
// and the scene supply their nodes; this places one under the other, and hands
// each choice to the one whose node it is.
class HierarchyOutline
{
public:
    static constexpr std::string_view kId = "hierarchy";

    HierarchyOutline(EditorWorkspace& workspace, WorldExplorer& entities, PartitionOutline& partition)
        : Workspace(workspace)
        , Entities(entities)
        , Partition(partition)
    {
    }

    // Its row in the workspace's explorers: opened as explorer.hierarchy, by Tab.
    [[nodiscard]] ExplorerRow Row();
    [[nodiscard]] ExplorerPage Explore(const ExplorerQuery& query);
    void Choose(std::string_view key, ExplorerChoice choice, std::size_t mark);

private:
    EditorWorkspace& Workspace;
    WorldExplorer& Entities;
    PartitionOutline& Partition;
    TreeOutline Outline;
};
