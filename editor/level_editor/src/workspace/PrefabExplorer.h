#pragma once

#include "workspaces/WorkspaceFacets.h"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

class EditorWorkspace;

//=============================================================================
// PrefabExplorer
//
// The scene sources under the project's content roots, as an explorer of
// prefabs to place: pictured, filtered by name, a page at a time. Choosing
// one puts it in hand and takes up the Place tool. The level being edited is
// never offered as a prefab of itself.
//=============================================================================
class PrefabExplorer
{
public:
    static constexpr std::string_view kId = "prefabs";

    // `picture` names the host image showing a prefab, loading it if it has
    // to; it is asked only for the page being shown.
    PrefabExplorer(EditorWorkspace& workspace, std::function<std::string(const std::string&)> picture);

    [[nodiscard]] static WorkspaceExplorerInfo Info();
    // Finds the scene sources again: after a save adds one, or when asked.
    void Rescan(const std::vector<std::filesystem::path>& roots);
    [[nodiscard]] ExplorerPage Explore(const ExplorerQuery& query) const;
    void Choose(std::string_view key, ExplorerChoice choice);

private:
    struct Prefab
    {
        std::string AssetPath;
        std::string Label;
    };

    EditorWorkspace& Workspace;
    std::function<std::string(const std::string&)> Picture;
    std::vector<Prefab> Prefabs;
};
