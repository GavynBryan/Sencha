#include "workspace/PrefabExplorer.h"

#include "document/WorldDocument.h"
#include "tools/ToolRegistry.h"
#include "workspace/EditorWorkspace.h"

#include <SDL3/SDL_keycode.h>

PrefabExplorer::PrefabExplorer(EditorWorkspace& workspace)
    : Workspace(workspace)
{
}

ExplorerRow PrefabExplorer::Row()
{
    return { .Id = std::string(kId),
             .Title = "Prefabs",
             .Icon = IconId::Box,
             .Chords = { { SDLK_P, { .Shift = true } } },
             .Choose = [this](std::string_view key, ExplorerChoice choice, std::size_t) { Choose(key, choice); },
             .Shape = ExplorerShape::Files,
             .Describe = [this](WorkspaceExplorerInfo& info) { Describe(info); } };
}

void PrefabExplorer::Describe(WorkspaceExplorerInfo& info) const
{
    info.Accepts = { { ".sscene" } };
    info.Current = Workspace.Resources.Active(AssetType::Scene).Path;
    info.Withheld.clear();
    if (std::string own = Workspace.World.FocusDocument().SourceAssetPath(); !own.empty())
        info.Withheld.push_back(std::move(own));
}

void PrefabExplorer::Choose(std::string_view key, ExplorerChoice)
{
    Workspace.Resources.SetActive(AssetRef{ AssetType::Scene, std::string(key) });
    if (ToolRegistry* tools = Workspace.Interaction.Tools.get())
        (void)tools->Activate("place");
}
