#include "app/ProjectSession.h"

#include "document/LevelConventions.h"

#include "project/AssetAdoption.h"
#include "project/ProjectContentMount.h"
#include "project/ProjectFileSteps.h"
#include "ui/MaterialThumbnailCache.h"

#include <app/Engine.h>
#include <app/Game.h>
#include <app/GameDataAssets.h>
#include <app/RuntimeContent.h>
#include <assets/cook/ContentImporters.h>
#include <assets/hotreload/SourceReloadRoots.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/console/ConsoleService.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <ui/UiService.h>

#include <cstdio>
#include <cstdlib>
#include <utility>

ProjectSession::ProjectSession(Engine& engine, std::optional<std::string> projectPath)
    : Host(engine)
    , Importers(&engine.Jobs())
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

    // The editor's own data asset types and the module's, before the content
    // that holds them is scanned.
    RegisterLevelConventionsData(Assets().DataTypes, Assets().DataSchemas);
    LoadModule(modulePath);
    MountContent();
    if (!Descriptor)
        return;
    MaterialList.Rescan(Descriptor->ContentRoots);
    FileSteps = std::make_unique<ProjectFileSteps>(
        std::vector<std::filesystem::path>(Descriptor->ContentRoots.begin(), Descriptor->ContentRoots.end()), Journal,
        ProjectFileHooks{
            .IsAsset = [this](const ProjectFile& file) { return IsAsset(file); },
            .Changed = [this](const ProjectFileChange& change) { FileChanged(change); },
        });
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

void ProjectSession::UpdatePictures()
{
    if (MaterialPictures == nullptr)
    {
        UiService* ui = Host.TryUi();
        GraphicsServices* graphics = Host.TryGraphics();
        if (ui == nullptr || !ui->IsReady() || graphics == nullptr)
            return;
        MaterialThumbnailCache::RegisterBudget(Host.Console().Registry());
        RuntimeAssets& assets = Assets();
        MaterialPictures = std::make_unique<MaterialThumbnailCache>(assets.Assets, *assets.Textures, assets.Registry,
                                                                    ui, &graphics->MainRenderer.GetUiTextures(),
                                                                    "picture:");
        MaterialPictureFiles = PictureSources.Provide(".smat", *MaterialPictures);
        TexturePictureFiles = PictureSources.Provide(".png", *MaterialPictures);
    }
    MaterialPictures->UpdateFrame(Host);
}

bool ProjectSession::IsAsset(const ProjectFile& file) const
{
    return IsAssetFile(file, Assets().Assets.Kinds(), Importers.Registry());
}

void ProjectSession::FileChanged(const ProjectFileChange& change)
{
    MountProjectFileChange(change, Assets(), Importers.Registry(), Host.Logging());
    if (Descriptor)
        MaterialList.Rescan(Descriptor->ContentRoots);
    PictureSources.Forget();
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
    {
        // Before the scan, so the registry reads the ids the files now state.
        const std::vector<std::filesystem::path> roots(Descriptor->ContentRoots.begin(), Descriptor->ContentRoots.end());
        Adopted = AdoptAssetIds(roots, [this](const ProjectFile& file) { return IsAsset(file); }, AdoptionFailures);
        MountProjectContent(*Descriptor, assets, Host.Logging(), &Host.Jobs());
    }
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
