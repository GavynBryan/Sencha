#include "workspace/HeldResources.h"

#include "project/MaterialLibrary.h"
#include "resources/ResourceActions.h"
#include "scene_source/SceneSourcePaths.h"
#include "thumbnails/AssetPictures.h"
#include "tools/ToolRegistry.h"
#include "workspace/EditorWorkspace.h"
#include "workspace/MaterialExplorer.h"
#include "workspace/PrefabExplorer.h"

#include <algorithm>
#include <span>
#include <string>

AssetType HeldType(const EditorWorkspace& workspace)
{
    const ToolRegistry* tools = workspace.Interaction.Tools.get();
    const ITool* tool = tools != nullptr ? tools->GetActiveTool() : nullptr;
    if (tool != nullptr)
        for (const ResourceRole& role : tool->Resources())
            if (role.Primary)
                return role.Type;
    return AssetType::Material;
}

WorkspaceResourceFacet HeldResourceFacet(const EditorWorkspace& workspace, AssetPictures& pictures)
{
    const AssetType type = HeldType(workspace);
    const bool prefab = type == AssetType::Scene;
    const auto held = [&](const AssetRef& ref) -> HeldResource {
        if (!ref.IsValid())
            return {};
        return { .Label = prefab ? std::string(SceneSourceStem(ref.Path)) : MaterialLeafName(ref.Path),
                 .Image = pictures.PictureOf(ref.Path).Image };
    };
    WorkspaceResourceFacet facet{ .Role = prefab ? "Prefab" : "Material",
                                  .Primary = true,
                                  .Explorer = std::string(prefab ? PrefabExplorer::kId : MaterialExplorer::kId),
                                  .Active = held(workspace.Resources.Active(type)) };
    const std::span<const AssetRef> stash = workspace.Resources.Stash(type);
    for (std::size_t slot = 0; slot < stash.size(); ++slot)
    {
        HeldResource stashed = held(stash[slot]);
        stashed.Action = UseSlotAction(slot);
        facet.Stash.push_back(std::move(stashed));
    }
    return facet;
}

std::vector<ContextMenuModel::AssetChoice> AssetsToHand(const EditorWorkspace& workspace, AssetType type,
                                                        AssetPictures& pictures)
{
    std::vector<ContextMenuModel::AssetChoice> choices;
    const auto add = [&](const AssetRef& ref) {
        if (ref.Path.empty() || std::ranges::any_of(choices, [&](const auto& c) { return c.Path == ref.Path; }))
            return;
        choices.push_back({ .Path = ref.Path,
                            .Label = MaterialLeafName(ref.Path),
                            .Image = type == AssetType::Material ? pictures.PictureOf(ref.Path).Image
                                                                 : std::string{} });
    };
    add(workspace.Resources.Active(type));
    for (const AssetRef& stashed : workspace.Resources.Stash(type))
        add(stashed);
    return choices;
}
