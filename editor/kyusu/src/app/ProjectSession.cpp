#include "app/ProjectSession.h"

#include "project/ProjectContentMount.h"

#include <app/Engine.h>
#include <app/Game.h>
#include <app/GameDataAssets.h>
#include <app/RuntimeContent.h>
#include <assets/runtime/RuntimeAssets.h>

#include <cstdio>
#include <cstdlib>
#include <utility>

ProjectSession::ProjectSession(Engine& engine, std::optional<std::string> projectPath)
    : Host(engine)
    , MaterialList(engine.Logging())
{
    std::string modulePath;
    if (projectPath)
    {
        ProjectDescriptor descriptor;
        std::string error;
        if (ProjectDescriptor::Load(*projectPath, descriptor, &error))
        {
            Descriptor = std::move(descriptor);
            modulePath = Descriptor->GameModulePath;
            std::fprintf(stderr, "[kyusu] opened project '%s' (%s)\n", Descriptor->Name.c_str(),
                         projectPath->c_str());
        }
        else
        {
            std::fprintf(stderr, "[kyusu] failed to open project '%s': %s\n", projectPath->c_str(),
                         error.c_str());
        }
    }
    else if (const char* envPath = std::getenv("SENCHA_GAME_MODULE"); envPath != nullptr && envPath[0] != '\0')
    {
        modulePath = envPath;
    }

    // The module's data asset types before the content that holds them is scanned.
    LoadModule(modulePath);
    MountContent();
    if (Descriptor)
        MaterialList.Rescan(Descriptor->ContentRoots);
}

ProjectSession::~ProjectSession()
{
    if (!GameModule.IsValid())
        return;
    UnregisterGameDataAssets(*GameModule.Instance, Assets());
    ModuleLoader.Unload(GameModule);
}

RuntimeAssets& ProjectSession::Assets() const
{
    return Host.Content().Assets();
}

void ProjectSession::LoadModule(const std::string& modulePath)
{
    if (modulePath.empty())
        return;
    std::string error;
    GameModule = ModuleLoader.Load(modulePath, &error);
    if (!GameModule.IsValid())
    {
        std::fprintf(stderr, "[kyusu] failed to load game module '%s': %s\n", modulePath.c_str(), error.c_str());
        return;
    }
    // Workspaces borrow the module's registrations and never run its game, but
    // a data asset type may still reach the engine it was written against.
    GameModule.Instance->AttachEngine(Host);
    RegisterGameDataAssets(*GameModule.Instance, Assets());
    std::fprintf(stderr, "[kyusu] loaded game module '%s'\n", modulePath.c_str());
}

void ProjectSession::MountContent()
{
    // Into the engine's stack, the one Engine::Ui() resolves packages through
    // and the one the engine already mounted its own content into, so every
    // workspace and the authored UI see each asset once.
    RuntimeAssets& assets = Assets();
    if (Descriptor)
        MountProjectContent(*Descriptor, assets, Host.Logging(), &Host.Jobs());
#if defined(SENCHA_ENABLE_UI) && defined(SENCHA_EDITOR_UI_DIR)
    MountEditorContent(SENCHA_EDITOR_UI_DIR, assets, Host.Logging(), &Host.Jobs());
#endif
}
