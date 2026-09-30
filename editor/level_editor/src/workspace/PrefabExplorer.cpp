#include "workspace/PrefabExplorer.h"

#include "document/WorldDocument.h"
#include "scene_source/SceneSourcePaths.h"
#include "text/TextFilterMatch.h"
#include "tools/ToolRegistry.h"
#include "workspace/EditorWorkspace.h"

#include <algorithm>

PrefabExplorer::PrefabExplorer(EditorWorkspace& workspace, std::function<std::string(const std::string&)> picture)
    : Workspace(workspace)
    , Picture(std::move(picture))
{
}

WorkspaceExplorerInfo PrefabExplorer::Info()
{
    return { .Id = std::string(kId), .Title = "Prefabs", .Shape = ExplorerShape::Pictures,
             .Action = "explorer.prefabs" };
}

void PrefabExplorer::Rescan(const std::vector<std::filesystem::path>& roots)
{
    Prefabs.clear();
    for (const std::filesystem::path& root : roots)
    {
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec))
            continue;
        for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec))
        {
            if (!it->is_regular_file(ec) || it->path().extension() != ".sscene")
                continue;
            std::error_code relative;
            const std::filesystem::path path = std::filesystem::relative(it->path(), root, relative);
            if (relative)
                continue;
            std::string assetPath = "asset://" + path.generic_string();
            std::string label(SceneSourceStem(assetPath));
            Prefabs.push_back({ std::move(assetPath), std::move(label) });
        }
    }
    std::ranges::sort(Prefabs, {}, &Prefab::AssetPath);
}

ExplorerPage PrefabExplorer::Explore(const ExplorerQuery& query) const
{
    ExplorerPage page;
    page.Actions = { "prefabs.rescan" };
    page.Empty = Prefabs.empty() ? "No scene sources in the project's content roots." : "No prefab's name holds that.";
    const std::string_view editing = Workspace.World.FocusDocument().GetDisplayName();
    const std::string& held = Workspace.Resources.Active(AssetType::Scene).Path;
    for (const Prefab& prefab : Prefabs)
    {
        const bool self = !editing.empty() && editing.ends_with(prefab.AssetPath.substr(sizeof("asset://") - 1));
        if (self || !TextFilterMatch(prefab.Label, query.Filter))
            continue;
        if (!CountOnPage(page, query))
            continue;
        page.Items.push_back({ .Key = prefab.AssetPath,
                               .Label = prefab.Label,
                               .Image = Picture ? Picture(prefab.AssetPath) : std::string{},
                               .Current = prefab.AssetPath == held });
    }
    return page;
}

void PrefabExplorer::Choose(std::string_view key, ExplorerChoice)
{
    Workspace.Resources.SetActive(AssetRef{ AssetType::Scene, std::string(key) });
    if (ToolRegistry* tools = Workspace.Interaction.Tools.get())
        (void)tools->Activate("place");
}
