#pragma once

#include "workspaces/WorkspaceFacets.h"

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

    [[nodiscard]] WorkspaceExplorerInfo Info() const;
    void Choose(std::string_view key, ExplorerChoice choice);

private:
    EditorWorkspace& Workspace;
};
