#include "workspace/MaterialExplorer.h"

#include "project/MaterialLibrary.h"
#include "text/TextFilterMatch.h"
#include "workspace/EditorWorkspace.h"

MaterialExplorer::MaterialExplorer(MaterialLibrary& materials, EditorWorkspace& workspace,
                                   std::function<std::string(const std::string&)> picture)
    : Materials(materials)
    , Workspace(workspace)
    , Picture(std::move(picture))
{
}

WorkspaceExplorerInfo MaterialExplorer::Info()
{
    return { .Id = std::string(kId), .Title = "Materials", .Shape = ExplorerShape::Pictures,
             .Action = "explorer.materials" };
}

ExplorerPage MaterialExplorer::Explore(const ExplorerQuery& query) const
{
    ExplorerPage page;
    page.Actions = { "materials.rescan" };
    page.Empty = Materials.Materials().empty() ? "No materials in the project's content roots."
                                               : "No material's name holds that.";
    const std::string& active = Workspace.Resources.Active(AssetType::Material).Path;
    for (const MaterialAsset& material : Materials.Materials())
    {
        if (!TextFilterMatch(material.DisplayName, query.Filter))
            continue;
        const std::size_t index = page.Total++;
        if (index < query.First || page.Items.size() >= query.Count)
            continue;
        page.Items.push_back({ .Key = material.Path,
                               .Label = MaterialLeafName(material.Path),
                               .Image = Picture ? Picture(material.Path) : std::string{},
                               .Current = material.Path == active });
    }
    return page;
}

void MaterialExplorer::Choose(std::string_view key, ExplorerChoice choice)
{
    Workspace.Resources.SetActive(AssetRef{ AssetType::Material, std::string(key) });
    if (choice == ExplorerChoice::Commit)
        Workspace.ApplyActiveMaterialToSelectedFaces();
}
