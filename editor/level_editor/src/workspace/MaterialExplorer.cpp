#include "workspace/MaterialExplorer.h"

#include "workspace/EditorWorkspace.h"

MaterialExplorer::MaterialExplorer(MaterialLibrary& materials, EditorWorkspace& workspace,
                                   std::function<std::string(const std::string&)> picture)
    : Materials(materials)
    , Workspace(workspace)
    , Picture(std::move(picture))
{
}

ExplorerPage MaterialExplorer::Explore(const ExplorerQuery& query) const
{
    return MaterialLibraryPage(Materials, query, Workspace.Resources.Active(AssetType::Material).Path, Picture);
}

void MaterialExplorer::Choose(std::string_view key, ExplorerChoice choice)
{
    Workspace.Resources.SetActive(AssetRef{ AssetType::Material, std::string(key) });
    if (choice == ExplorerChoice::Commit)
        Workspace.ApplyActiveMaterialToSelectedFaces();
}
