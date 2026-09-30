#pragma once

#include "workspaces/WorkspaceFacets.h"

#include <functional>
#include <string>
#include <string_view>

class EditorWorkspace;
class MaterialLibrary;

//=============================================================================
// MaterialExplorer
//
// The project's materials as an explorer: pictured, filtered by name, a page
// at a time. Picking one makes it the material the next face operation uses;
// putting one to use also applies it to the selected faces.
//=============================================================================
class MaterialExplorer
{
public:
    static constexpr std::string_view kId = "materials";

    // `picture` names the host image showing a material, loading it if it has
    // to; it is asked only for the page being shown.
    MaterialExplorer(MaterialLibrary& materials, EditorWorkspace& workspace,
                     std::function<std::string(const std::string&)> picture);

    [[nodiscard]] static WorkspaceExplorerInfo Info();
    [[nodiscard]] ExplorerPage Explore(const ExplorerQuery& query) const;
    void Choose(std::string_view key, ExplorerChoice choice);

private:
    MaterialLibrary& Materials;
    EditorWorkspace& Workspace;
    std::function<std::string(const std::string&)> Picture;
};
