#pragma once

#include "workspaces/WorkspaceFacets.h"

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

    [[nodiscard]] WorkspaceExplorerInfo Info() const;
    void Choose(std::string_view key, ExplorerChoice choice);

private:
    EditorWorkspace& Workspace;
};
