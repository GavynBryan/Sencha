#include "app/ProjectSession.h"

#include "project/ProjectContentMount.h"

#include <app/Engine.h>
#include <app/Game.h>
#include <app/GameDataAssets.h>
#include <app/RuntimeContent.h>
#include <assets/hotreload/SourceReloadRoots.h>
#include <assets/runtime/RuntimeAssets.h>

#include <cstdio>
#include <cstdlib>
#include <utility>

ProjectSession::ProjectSession(Engine& engine, std::optional<std::string> projectPath)
    : Host(engine)
    , MaterialList(engine.Logging())
    , DataDocumentFiles(engine.Content().Assets(), Journal)
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
    if (SourceReloadRoots* sources = Host.Content().SourceReload())
        sources->SetReloadFilter({});
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
    WatchSources();
}

void ProjectSession::WatchSources()
{
    // Every root in the stack, the engine's own included, for every authored
    // source a workspace shows live: materials and their textures, data, and
    // the documents, stylesheets and fonts of authored UI.
    SourceReloadRoots* sources = Host.Content().SourceReload();
    if (sources == nullptr)
        return;
    // A changed source an open document holds its own version of stays out of
    // the stack: the document pushed its working version there.
    sources->SetReloadFilter([this](const std::filesystem::path& file) {
        return Journal.FileChangedOnDisk(file) != ExternalChange::Held;
    });
    const std::vector<std::string> extensions{ ".smat", ".sdata", ".png", ".meta", ".rml", ".rcss", ".ttf", ".otf" };
    for (const ContentRootPaths& root : Host.Content().Roots())
        sources->AddRoot(root.Authored.string(), extensions);
    if (Descriptor)
        for (const std::string& root : Descriptor->ContentRoots)
            sources->AddRoot(root, extensions);
#if defined(SENCHA_ENABLE_UI) && defined(SENCHA_EDITOR_UI_DIR)
    sources->AddRoot(SENCHA_EDITOR_UI_DIR, extensions);
#endif
}
