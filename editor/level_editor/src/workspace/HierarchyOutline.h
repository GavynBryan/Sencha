#pragma once

#include "workspaces/WorkspaceFacets.h"

#include <string_view>

class EditorWorkspace;
class PartitionOutline;
class WorldExplorer;

//=============================================================================
// HierarchyOutline
//
// The level as one outline: the world, its graphs and their zones, with the
// entities of what is being edited -- the focused zone, or the world scene --
// under it. A level that is not a world is its entities alone. The partition
// supplies the zone rows and the scene the entity rows; this only interleaves
// them, and hands each choice to the one whose row it is.
//=============================================================================
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

    [[nodiscard]] static WorkspaceExplorerInfo Info();
    [[nodiscard]] ExplorerPage Explore(const ExplorerQuery& query);
    void Choose(std::string_view key, ExplorerChoice choice, std::size_t mark);

    // An entity's key in this outline.
    [[nodiscard]] static std::string EntityKey(std::string_view entityKey);

private:
    EditorWorkspace& Workspace;
    WorldExplorer& Entities;
    PartitionOutline& Partition;
};
