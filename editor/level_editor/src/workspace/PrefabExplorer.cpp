#include "workspace/PrefabExplorer.h"

#include "document/WorldDocument.h"
#include "tools/ToolRegistry.h"
#include "workspace/EditorWorkspace.h"

PrefabExplorer::PrefabExplorer(EditorWorkspace& workspace)
    : Workspace(workspace)
{
}

WorkspaceExplorerInfo PrefabExplorer::Info() const
{
    WorkspaceExplorerInfo info{ .Id = std::string(kId),
                                .Title = "Prefabs",
                                .Shape = ExplorerShape::Files,
                                .Action = "explorer.prefabs",
                                .Accepts = { { ".sscene" } },
                                .Current = Workspace.Resources.Active(AssetType::Scene).Path };
    if (std::string own = Workspace.World.FocusDocument().SourceAssetPath(); !own.empty())
        info.Withheld.push_back(std::move(own));
    return info;
}

void PrefabExplorer::Choose(std::string_view key, ExplorerChoice)
{
    Workspace.Resources.SetActive(AssetRef{ AssetType::Scene, std::string(key) });
    if (ToolRegistry* tools = Workspace.Interaction.Tools.get())
        (void)tools->Activate("place");
}
