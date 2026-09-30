#pragma once

#include "project/MaterialLibrary.h"
#include "project/Project.h"

#include <app/GameModuleLoader.h>

#include <optional>
#include <string>

class Engine;
class Game;
struct RuntimeAssets;

//=============================================================================
// ProjectSession
//
// The project one Kyusu process edits, held once for every workspace: its
// descriptor, its game module, its content mounted into the engine's asset
// stack, and the materials it offers. Workspaces are built after it and
// destroyed before it, so the module stays mapped while anything it compiled
// is alive.
//=============================================================================
class ProjectSession
{
public:
    // projectPath is the resolved .senchaproj (ResolveProjectPath), if any.
    // Without one, SENCHA_GAME_MODULE may still name a bare game module.
    ProjectSession(Engine& engine, std::optional<std::string> projectPath);
    ~ProjectSession();

    ProjectSession(const ProjectSession&) = delete;
    ProjectSession& operator=(const ProjectSession&) = delete;

    // Mutable: cook profiles are edited into the descriptor and saved with it.
    [[nodiscard]] ProjectDescriptor* Project() { return Descriptor ? &*Descriptor : nullptr; }
    [[nodiscard]] Game* Module() const { return GameModule.Instance; }
    [[nodiscard]] RuntimeAssets& Assets() const;
    [[nodiscard]] MaterialLibrary& Materials() { return MaterialList; }

private:
    void LoadModule(const std::string& modulePath);
    void MountContent();
    void WatchSources();

    Engine& Host;
    std::optional<ProjectDescriptor> Descriptor;
    GameModuleLoader ModuleLoader;
    LoadedModule GameModule;
    MaterialLibrary MaterialList;
};
