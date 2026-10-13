#pragma once

#include "workspaces/ExplorerTable.h"

#include <string_view>

class EditorWorkspace;

//=============================================================================
// PrefabExplorer
//
// The project's scene sources as prefabs to place, chosen from the shelf.
// Choosing one puts it in hand and takes up the Entity tool. The level being
// edited is never offered as a prefab of itself.
//=============================================================================
class PrefabExplorer
{
public:
    static constexpr std::string_view kId = "prefabs";

    explicit PrefabExplorer(EditorWorkspace& workspace);

    // Its row in the workspace's explorers: opened as explorer.prefabs.
    [[nodiscard]] ExplorerRow Row();
    // The kinds it offers, the one in hand, and the level being edited withheld.
    void Describe(WorkspaceExplorerInfo& info) const;
    void Choose(std::string_view key, ExplorerChoice choice);

private:
    EditorWorkspace& Workspace;
};
