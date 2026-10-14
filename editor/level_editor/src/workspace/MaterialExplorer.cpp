#include "workspace/MaterialExplorer.h"

#include "project/MaterialLibraryActions.h"
#include "workspace/EditorWorkspace.h"

#include <utility>

MaterialExplorer::MaterialExplorer(EditorWorkspace& workspace)
    : Workspace(workspace)
{
}

ExplorerRow MaterialExplorer::Row()
{
    ActionDefinition action = MaterialExplorerAction();
    return { .Id = std::string(kId),
             .Title = std::move(action.Label),
             .Icon = action.Icon,
             .Chords = std::move(action.DefaultChords),
             .Choose = [this](std::string_view key, ExplorerChoice choice, std::size_t) { Choose(key, choice); },
             .Shape = ExplorerShape::Files,
             .Describe = [this](WorkspaceExplorerInfo& info) { Describe(info); } };
}

void MaterialExplorer::Describe(WorkspaceExplorerInfo& info) const
{
    info.Accepts = { { ".smat" } };
    info.Current = Workspace.Resources.Active(AssetType::Material).Path;
}

void MaterialExplorer::Choose(std::string_view key, ExplorerChoice choice)
{
    Workspace.Resources.SetActive(AssetRef{ AssetType::Material, std::string(key) });
    if (choice == ExplorerChoice::Commit)
        Workspace.ApplyActiveMaterialToSelectedFaces();
}
