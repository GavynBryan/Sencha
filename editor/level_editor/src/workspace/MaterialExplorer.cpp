#include "workspace/MaterialExplorer.h"

#include "workspace/EditorWorkspace.h"

MaterialExplorer::MaterialExplorer(EditorWorkspace& workspace)
    : Workspace(workspace)
{
}

WorkspaceExplorerInfo MaterialExplorer::Info() const
{
    return { .Id = std::string(kId),
             .Title = "Materials",
             .Shape = ExplorerShape::Files,
             .Action = "explorer.materials",
             .Accepts = { { ".smat" } },
             .Current = Workspace.Resources.Active(AssetType::Material).Path };
}

void MaterialExplorer::Choose(std::string_view key, ExplorerChoice choice)
{
    Workspace.Resources.SetActive(AssetRef{ AssetType::Material, std::string(key) });
    if (choice == ExplorerChoice::Commit)
        Workspace.ApplyActiveMaterialToSelectedFaces();
}
