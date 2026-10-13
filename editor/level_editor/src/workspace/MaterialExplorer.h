#pragma once

#include "workspaces/ExplorerTable.h"

#include <string_view>

class EditorWorkspace;

//=============================================================================
// MaterialExplorer
//
// The project's materials, chosen from the shelf. Picking one makes it the
// material the next face operation uses; committing to one also applies it to
// the selected faces.
//=============================================================================
class MaterialExplorer
{
public:
    static constexpr std::string_view kId = "materials";

    explicit MaterialExplorer(EditorWorkspace& workspace);

    // Its row in the workspace's explorers: opened as explorer.materials.
    [[nodiscard]] ExplorerRow Row();
    // The kinds it offers and the one in hand.
    void Describe(WorkspaceExplorerInfo& info) const;
    void Choose(std::string_view key, ExplorerChoice choice);

private:
    EditorWorkspace& Workspace;
};
