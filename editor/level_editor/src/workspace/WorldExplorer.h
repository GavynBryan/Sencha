#pragma once

#include "workspaces/WorkspaceFacets.h"

#include <ecs/EntityId.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

class EditorWorkspace;

//=============================================================================
// WorldExplorer
//
// The scene being edited as an outline: every entity under its parent, each
// named, with its visibility and lock as marks. Choosing an item selects it,
// alone, as a range from the primary or beside the others; opening and closing
// branches is remembered by persistent id, so a branch keeps its state through
// undo and reload. A placement reads as one thing until it is opened, and a
// selection made elsewhere opens its ancestors.
//=============================================================================
class WorldExplorer
{
public:
    static constexpr std::string_view kId = "world";

    explicit WorldExplorer(EditorWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] static WorkspaceExplorerInfo Info();
    [[nodiscard]] ExplorerPage Explore(const ExplorerQuery& query);
    void Choose(std::string_view key, ExplorerChoice choice, std::size_t mark);

private:
    struct Row
    {
        EntityId Entity;
        int Depth = 0;
        ExplorerBranch Branch = ExplorerBranch::Leaf;
    };

    // Every row an outline filtered by `filter` shows, top to bottom.
    [[nodiscard]] std::vector<Row> Rows(std::string_view filter);
    [[nodiscard]] bool IsOpen(EntityId entity) const;
    void SetOpen(EntityId entity, bool open);
    [[nodiscard]] EntityId Resolve(std::string_view key) const;
    void Select(EntityId entity, ExplorerChoice choice);

    EditorWorkspace& Workspace;
    // Departures from each default, by persistent id: an ordinary branch is
    // open unless collapsed, a placement closed unless opened.
    std::unordered_set<std::uint64_t> Collapsed;
    std::unordered_set<std::uint64_t> OpenedPlacements;
    EntityId Revealed{};
    // The rows as last explored, which a range selects across.
    std::vector<EntityId> Shown;
};
