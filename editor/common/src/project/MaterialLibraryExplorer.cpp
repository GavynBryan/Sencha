#include "project/MaterialLibraryExplorer.h"

#include "project/MaterialLibrary.h"
#include "text/TextFilterMatch.h"

#include <SDL3/SDL_keycode.h>

WorkspaceExplorerInfo MaterialExplorerInfo()
{
    return { .Id = std::string(kMaterialExplorerId), .Title = "Materials", .Shape = ExplorerShape::Pictures,
             .Action = "explorer.materials" };
}

ActionDefinition MaterialExplorerAction()
{
    return { .Id = "explorer.materials", .Label = "Materials", .Category = "Explore", .Icon = IconId::Folder,
             .DefaultChords = { { SDLK_M, {} } } };
}

ActionDefinition MaterialRescanAction()
{
    return { .Id = "materials.rescan", .Label = "Rescan Materials", .Category = "Material", .Icon = IconId::Refresh };
}

ExplorerPage MaterialLibraryPage(const MaterialLibrary& materials, const ExplorerQuery& query, std::string_view current,
                                 const std::function<std::string(const std::string&)>& picture)
{
    ExplorerPage page;
    page.Actions = { "materials.rescan" };
    page.Empty = materials.Materials().empty() ? "No materials in the project's content roots."
                                               : "No material's name holds that.";
    for (const MaterialAsset& material : materials.Materials())
    {
        if (!TextFilterMatch(material.DisplayName, query.Filter) || !CountOnPage(page, query))
            continue;
        page.Items.push_back({ .Key = material.Path,
                               .Label = MaterialLeafName(material.Path),
                               .Image = picture ? picture(material.Path) : std::string{},
                               .Current = material.Path == current });
    }
    return page;
}
