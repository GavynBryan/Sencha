#include "MaterialEditorServices.h"

#include "MaterialBrowserPanel.h"
#include "MaterialInspectorPanel.h"
#include "MaterialPreviewPanel.h"
#include "MaterialPreviewRenderFeature.h"
#include "TexturesPanel.h"

#include "project/ProjectContentMount.h"
#include "ui/EditorThemeFile.h"
#include "ui/EditorThemeStartup.h"
#include "ui/EditorUiFeature.h"
#include "ui/ImGuiTextureBinding.h"

#include <SDL3/SDL.h>

#include <app/Engine.h>
#include <app/EngineSchedule.h>
#include <app/Game.h>
#include <assets/cook/AssetImporter.h>
#include <assets/cook/TextureCook.h>
#include <assets/cook/TextureImportSettings.h>
#include <assets/hotreload/AssetHotReloader.h>
#include <assets/material/MaterialAssetLoader.h>
#include <assets/material/MaterialWriter.h>
#include <core/assets/AssetRegistry.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <core/console/ConsoleTypes.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/Renderer.h>
#include <platform/SdlWindow.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string_view>
#include <vector>

namespace
{
// Same edge as kyusu: the preview feature's teardown releases ImGui texture
// bindings through the backend the UI feature owns.
constexpr std::string_view kPreviewFeatureId = "material_preview";
constexpr std::string_view kUiFeatureId = "editor_ui";
constexpr std::array<std::string_view, 1> kPreviewDependsOn{ kUiFeatureId };
} // namespace
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <variant>

namespace
{
    // Minimal frame hook (same shape as the level editor's): one callback per
    // frame for deferred work off the event path.
    class FrameHook
    {
    public:
        explicit FrameHook(std::function<void()> fn) : Fn(std::move(fn)) {}
        void FrameUpdate(FrameUpdateContext&) { if (Fn) Fn(); }

    private:
        std::function<void()> Fn;
    };
}

struct MaterialEditorServices::TextureRecookState
{
    explicit TextureRecookState(JobSystem* jobs)
        : TextureImporter(jobs)
    {
    }

    struct RootReloader
    {
        std::string Root;
        AssetHotReloader Reloader;
    };

    PngTextureImporter TextureImporter;
    AssetImporterRegistry Importers;
    std::vector<std::unique_ptr<RootReloader>> Roots;
};

MaterialEditorServices::MaterialEditorServices(Engine& engine,
                                               SdlWindow& window,
                                               const EngineConfig&,
                                               std::optional<std::string> projectPath)
    : ProjectPath(std::move(projectPath))
{
    EnginePtr = &engine;
    Window = &window;

    LoadProject();
    InitAssets();
    BuildUi();
}

MaterialEditorServices::~MaterialEditorServices()
{
    // Remove the preview feature while the caches it borrows still live: the
    // renderer would otherwise hold it until ~Renderer, well after the asset
    // system below is gone. Removal runs its teardown here.
    if (Preview != nullptr && EnginePtr != nullptr)
    {
        if (GraphicsServices* graphics = EnginePtr->TryGraphics())
            graphics->MainRenderer.RemoveFeature(Preview);
        Preview = nullptr;
    }
    // Panel-owned bindings still release inline; the panel belongs to the UI
    // feature, which tears down in ~Renderer.
    if (Textures != nullptr)
        Textures->ReleasePreviewResources();
    if (Assets)
        for (const auto& tab : Tabs.Tabs())
            if (tab->Handle.IsValid())
                Assets->Assets.ReleaseMaterial(tab->Handle);
    TextureRecook.reset();
    Assets.reset();
}

void MaterialEditorServices::LoadProject()
{
    if (!ProjectPath)
    {
        std::fprintf(stderr, "[shudei] no project: pass --project <path.senchaproj> or set SENCHA_PROJECT\n");
        return;
    }

    ProjectDescriptor descriptor;
    std::string error;
    if (!ProjectDescriptor::Load(*ProjectPath, descriptor, &error))
    {
        std::fprintf(stderr, "[shudei] failed to open project '%s': %s\n",
                     ProjectPath->c_str(), error.c_str());
        return;
    }
    Project = std::move(descriptor);
    std::fprintf(stderr, "[shudei] opened project '%s' (%s)\n",
                 Project->Name.c_str(), ProjectPath->c_str());
}

void MaterialEditorServices::InitAssets()
{
    Engine& engine = *EnginePtr;
    GraphicsServices& graphics = engine.Graphics();

    Assets.emplace(engine.Logging(), graphics.Buffers, graphics.Images,
                   graphics.Descriptors, graphics.Samplers);
    Materials = std::make_unique<MaterialLibrary>(engine.Logging());
    if (!Project)
        return;

    MountProjectContent(*Project, *Assets, engine.Logging(), &engine.Jobs());
    Materials->Rescan(Project->ContentRoots);

    TextureRecook = std::make_unique<TextureRecookState>(&engine.Jobs());
    TextureRecook->Importers.Register(TextureRecook->TextureImporter);
    for (const std::string& root : Project->ContentRoots)
        TextureRecook->Roots.push_back(std::unique_ptr<TextureRecookState::RootReloader>(
            new TextureRecookState::RootReloader{
                root,
                AssetHotReloader(engine.Logging(), Assets->Assets, Assets->Registry,
                                 TextureRecook->Importers, engine.Tasks(), root),
            }));
}

void MaterialEditorServices::BuildUi()
{
    Engine& engine = *EnginePtr;
    ApplyEditorThemeFromConsole(engine.Console(), "Shudei");
    RegisterPreviewBackdropCVars();

    Renderer& renderer = engine.Graphics().MainRenderer;

    auto preview = std::make_unique<MaterialPreviewRenderFeature>(*Assets);
    Preview = renderer.StageFeature(
        std::move(preview),
        FeatureRegistration{ .Id = kPreviewFeatureId,
                             .DependsOn = kPreviewDependsOn });

    auto uiFeature = std::make_unique<EditorUiFeature>(
        engine, *Window, engine.Graphics().Instance, engine.Graphics().Frames,
        "shudei.imgui.ini");
    // Provisional, for the panel wiring below; reassigned from what AddFeature
    // returns once the feature is registered.
    UiFeature = uiFeature.get();
    UiFeature->SetUndoActions(
        [this]() { if (MaterialEditTab* tab = Tabs.Active()) tab->Commands.Undo(); },
        [this]() { if (MaterialEditTab* tab = Tabs.Active()) tab->Commands.Redo(); },
        [this]() { MaterialEditTab* tab = Tabs.Active(); return tab != nullptr && tab->Commands.CanUndo(); },
        [this]() { MaterialEditTab* tab = Tabs.Active(); return tab != nullptr && tab->Commands.CanRedo(); });
    UiFeature->SetFileActions(
        {},
        {},
        [this]() { SaveActiveMaterial(); },
        {});
    UiFeature->SetSaveAllAction([this]() { SaveAllMaterials(); });

    UiFeature->AddPanel(std::make_unique<MaterialBrowserPanel>(
        *Materials, Tabs,
        MaterialBrowserPanel::Actions{
            .Open = [this](const std::string& path) { OpenMaterial(path); },
            .CreateNew = [this](const std::string& name) { CreateMaterial(name, false); },
            .Duplicate = [this](const std::string& name) { CreateMaterial(name, true); },
            .Rename = [this](const std::string& path, const std::string& newRelPath)
            { RenameMaterial(path, newRelPath); },
            .Rescan = [this]() { RescanMaterials(); },
        }));
    UiFeature->AddPanel(std::make_unique<MaterialInspectorPanel>(
        Tabs, Assets->Registry,
        [this](const std::string& virtualPath)
        { if (Textures != nullptr) Textures->SelectTexture(virtualPath); }));

    auto texturesPanel = std::make_unique<TexturesPanel>(
        Assets->Registry,
        Project ? Project->ContentRoots : std::vector<std::string>{},
        [this](const TextureSourceLocation& source, std::string* error)
        { return RecookTexture(source, error); },
        std::make_unique<ImGuiTextureBinding>(
            Assets->Assets, *Assets->Textures,
            engine.Graphics().Images, engine.Graphics().Samplers),
        [this](const std::string& textureVirtualPath)
        { CreateMaterialFromTexture(textureVirtualPath); });
    Textures = texturesPanel.get();
    UiFeature->AddPanel(std::move(texturesPanel));
    // The preview panel holds a reference, so it can only exist if the feature
    // was staged. Whether its setup succeeds is reported by the commit below.
    if (Preview != nullptr)
    {
        UiFeature->AddPanel(std::make_unique<MaterialPreviewPanel>(
            *Preview, Tabs, [this](std::size_t index) { CloseTab(index); }));
    }
    else
    {
        std::fprintf(stderr, "[shudei] preview render feature failed to set up; "
                             "the preview panel is unavailable\n");
    }

    renderer.StageFeature(std::move(uiFeature),
                          FeatureRegistration{ .Id = kUiFeatureId });

    // Commit both: setup runs in dependency order, and a failure takes the
    // panels the feature owns -- Textures points into one, and the destructor
    // releases GPU refs through it.
    std::vector<std::string_view> failed;
    renderer.CommitStagedFeatures(&failed);
    const auto didFail = [&failed](std::string_view id)
    {
        return std::find(failed.begin(), failed.end(), id) != failed.end();
    };
    if (didFail(kUiFeatureId))
    {
        std::fprintf(stderr, "[shudei] UI feature failed to set up; "
                             "editor panels are unavailable\n");
        UiFeature = nullptr;
        Textures = nullptr;
    }
    if (didFail(kPreviewFeatureId))
        Preview = nullptr;
}

void MaterialEditorServices::RegisterPreviewBackdropCVars()
{
    ConsoleRegistry& registry = EnginePtr->Console().Registry();
    const auto registerDouble = [&registry](const char* name, double def, const char* help)
    {
        registry.RegisterCVar({
            .Name = name,
            .Owner = "editor",
            .Type = CVarType::Double,
            .DefaultValue = def,
            .CurrentValue = def,
            .Flags = CVarFlags::Archive,
            .Help = help,
            .Source = { "editor" },
        });
    };
    registerDouble("editor.preview.backdrop.cell_px", 64.0,
                   "Material preview backdrop: grid cell size in px.");
    registerDouble("editor.preview.backdrop.glow_px", 2.5,
                   "Material preview backdrop: glow halo width in px.");
    registerDouble("editor.preview.backdrop.intensity", 1.6,
                   "Material preview backdrop: line brightness (above 1 pushes into HDR).");
    registry.RegisterCVar({
        .Name = "editor.preview.backdrop.color",
        .Owner = "editor",
        .Type = CVarType::String,
        .DefaultValue = std::string("#1e8fff"),
        .CurrentValue = std::string("#1e8fff"),
        .Flags = CVarFlags::Archive,
        .Help = "Material preview backdrop: grid line color, #RRGGBB sRGB hex.",
        .Source = { "editor" },
    });
}

void MaterialEditorServices::UpdatePreviewBackdropStyle()
{
    if (Preview == nullptr)
        return;
    ConsoleRegistry& registry = EnginePtr->Console().Registry();
    const auto readDouble = [&registry](const char* name, float fallback)
    {
        if (const CVarMetadata* cvar = registry.FindCVar(name);
            cvar != nullptr && std::holds_alternative<double>(cvar->CurrentValue))
            return static_cast<float>(std::get<double>(cvar->CurrentValue));
        return fallback;
    };

    PreviewBackdropStyle style;
    style.CellPx = readDouble("editor.preview.backdrop.cell_px", style.CellPx);
    style.GlowPx = readDouble("editor.preview.backdrop.glow_px", style.GlowPx);
    const float intensity =
        readDouble("editor.preview.backdrop.intensity", style.LineColor[3]);
    if (const CVarMetadata* cvar = registry.FindCVar("editor.preview.backdrop.color");
        cvar != nullptr)
        if (const std::string* hex = std::get_if<std::string>(&cvar->CurrentValue))
        {
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
            float a = 1.0f;
            if (ParseThemeColor(*hex, r, g, b, a))
                style.LineColor = Vec4{ r, g, b, 1.0f };
        }
    style.LineColor[3] = intensity;
    Preview->BackdropStyle = style;
}

void MaterialEditorServices::RegisterSystems(EngineSchedule& schedule)
{
    schedule.Register<FrameHook>([this] { ProcessFrame(); });
}

void MaterialEditorServices::HandlePlatformEvent(PlatformEventContext& ctx)
{
    if (ctx.Event.type == SDL_EVENT_KEY_DOWN && !ctx.Event.key.repeat
        && (ctx.Event.key.mod & SDL_KMOD_CTRL) != 0)
    {
        const bool shift = (ctx.Event.key.mod & SDL_KMOD_SHIFT) != 0;
        MaterialEditTab* tab = Tabs.Active();
        switch (ctx.Event.key.scancode)
        {
        case SDL_SCANCODE_S:
            shift ? SaveAllMaterials() : SaveActiveMaterial();
            ctx.Handled = true;
            return;
        case SDL_SCANCODE_Z:
            if (tab != nullptr)
                shift ? tab->Commands.Redo() : tab->Commands.Undo();
            ctx.Handled = true;
            return;
        case SDL_SCANCODE_Y:
            if (tab != nullptr)
                tab->Commands.Redo();
            ctx.Handled = true;
            return;
        default:
            break;
        }
    }

    if (UiFeature != nullptr)
        UiFeature->ProcessSdlEvent(ctx.Event);
}

void MaterialEditorServices::ProcessFrame()
{
    for (const auto& tab : Tabs.Tabs())
        if (tab->Session.Version() != tab->AppliedVersion)
        {
            ApplyWorkingToResident(*tab);
            tab->AppliedVersion = tab->Session.Version();
        }

    // The preview follows the active tab (tab bar clicks change it without
    // going through OpenMaterial).
    MaterialEditTab* active = Tabs.Active();
    if (Preview != nullptr)
        Preview->SetMaterial(active != nullptr ? active->Handle : MaterialHandle{});
    UpdatePreviewBackdropStyle();

    UpdateTitle();
}

void MaterialEditorServices::OpenMaterial(const std::string& virtualPath)
{
    if (!Assets)
        return;
    const AssetRecord* record = Assets->Registry.FindByPath(virtualPath);
    if (record == nullptr || record->FilePath.empty())
    {
        std::fprintf(stderr, "[shudei] '%s' is not an editable material file\n", virtualPath.c_str());
        return;
    }

    std::string error;
    const bool existed = Tabs.Find(virtualPath) != nullptr;
    MaterialEditTab* tab = Tabs.OpenOrFocus(virtualPath, record->FilePath, &error);
    if (tab == nullptr)
    {
        std::fprintf(stderr, "[shudei] failed to open '%s': %s\n", virtualPath.c_str(), error.c_str());
        return;
    }

    if (!existed)
    {
        tab->Handle = Assets->Assets.LoadMaterial(virtualPath);
        // The resident material just loaded from disk, which is the saved
        // (and, right after Open, working) state.
        tab->AppliedVersion = tab->Session.Version();
    }
}

void MaterialEditorServices::CloseTab(std::size_t index)
{
    if (index >= Tabs.Tabs().size())
        return;
    if (Assets && Tabs.Tabs()[index]->Handle.IsValid())
        Assets->Assets.ReleaseMaterial(Tabs.Tabs()[index]->Handle);
    Tabs.Close(index);
}

void MaterialEditorServices::SaveActiveMaterial()
{
    MaterialEditTab* tab = Tabs.Active();
    if (tab == nullptr || !tab->Session.HasOpen())
        return;
    std::string error;
    if (!tab->Session.Save(&error))
        std::fprintf(stderr, "[shudei] save failed: %s\n", error.c_str());
}

void MaterialEditorServices::SaveAllMaterials()
{
    std::string error;
    Tabs.SaveAll(&error);
    if (!error.empty())
        std::fprintf(stderr, "[shudei] save all: %s\n", error.c_str());
}

void MaterialEditorServices::CreateMaterial(const std::string& name, bool duplicateOpen)
{
    if (!Assets || !Project || Project->ContentRoots.empty() || name.empty())
        return;
    MaterialEditTab* active = Tabs.Active();
    if (duplicateOpen && (active == nullptr || !active->Session.HasOpen()))
        return;

    const std::filesystem::path root(Project->ContentRoots.front());
    const std::filesystem::path file = root / "materials" / (name + ".smat");
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    if (std::filesystem::exists(file, ec))
    {
        std::fprintf(stderr, "[shudei] '%s' already exists\n", file.string().c_str());
        return;
    }

    std::string error;
    const bool written = duplicateOpen ? active->Session.SaveTo(file.string(), &error)
                                       : MaterialEditSession::CreateNew(file.string(), &error);
    if (!written)
    {
        std::fprintf(stderr, "[shudei] create failed: %s\n", error.c_str());
        return;
    }

    RescanMaterials();
    OpenMaterial("asset://materials/" + name + ".smat");
}

void MaterialEditorServices::RenameMaterial(const std::string& virtualPath,
                                            const std::string& newRelPath)
{
    if (!Assets || !Project || newRelPath.empty())
        return;
    const AssetRecord* record = Assets->Registry.FindByPath(virtualPath);
    if (record == nullptr || record->FilePath.empty())
    {
        std::fprintf(stderr, "[shudei] '%s' is not a renameable material file\n", virtualPath.c_str());
        return;
    }

    // The move stays inside the content root that owns the file, so the new
    // asset:// path is the new root-relative path.
    const std::filesystem::path oldFile(record->FilePath);
    std::filesystem::path owningRoot;
    for (const std::string& root : Project->ContentRoots)
    {
        const auto rel = oldFile.lexically_relative(root);
        if (!rel.empty() && *rel.begin() != "..")
        {
            owningRoot = root;
            break;
        }
    }
    if (owningRoot.empty())
    {
        std::fprintf(stderr, "[shudei] '%s' is outside every content root\n", record->FilePath.c_str());
        return;
    }

    std::string rel = newRelPath;
    if (rel.size() < 5 || rel.substr(rel.size() - 5) != ".smat")
        rel += ".smat";
    const std::filesystem::path newFile = (owningRoot / rel).lexically_normal();

    std::error_code ec;
    if (std::filesystem::exists(newFile, ec))
    {
        std::fprintf(stderr, "[shudei] '%s' already exists\n", newFile.string().c_str());
        return;
    }
    std::filesystem::create_directories(newFile.parent_path(), ec);
    std::filesystem::rename(oldFile, newFile, ec);
    if (ec)
    {
        std::fprintf(stderr, "[shudei] rename failed: %s\n", ec.message().c_str());
        return;
    }

    const std::string newVirtual = "asset://" + rel;
    // Levels referencing the old path are not rewritten; those faces render
    // as the level default until reassigned. Same policy as deleting a file.
    std::fprintf(stderr, "[shudei] renamed '%s' -> '%s' (level refs are not rewritten)\n",
                 virtualPath.c_str(), newVirtual.c_str());

    if (MaterialEditTab* tab = Tabs.Find(virtualPath))
    {
        tab->Session.RenameTo(newVirtual, newFile.string());
        if (tab->Handle.IsValid())
            Assets->Assets.ReleaseMaterial(tab->Handle);
        tab->Handle = MaterialHandle{};
    }

    RescanMaterials();

    if (MaterialEditTab* tab = Tabs.Find(newVirtual); tab != nullptr && !tab->Handle.IsValid())
    {
        tab->Handle = Assets->Assets.LoadMaterial(newVirtual);
        // Force a re-apply so an unsaved working state survives the move.
        tab->AppliedVersion = 0;
    }
}

void MaterialEditorServices::CreateMaterialFromTexture(const std::string& textureVirtualPath)
{
    if (!Assets || !Project)
        return;

    // "asset://textures/T-cliff.png" -> "textures/M-cliff.smat": beside the
    // texture, in the content root that owns its source file.
    const auto source = ResolveTextureSource(Project->ContentRoots, textureVirtualPath);
    if (!source)
    {
        std::fprintf(stderr, "[shudei] '%s' has no source file under any content root\n",
                     textureVirtualPath.c_str());
        return;
    }

    const std::size_t slash = source->RelPath.rfind('/');
    const std::string folder =
        slash == std::string::npos ? std::string{} : source->RelPath.substr(0, slash + 1);
    std::string name =
        slash == std::string::npos ? source->RelPath : source->RelPath.substr(slash + 1);
    if (const std::size_t dot = name.rfind('.'); dot != std::string::npos)
        name.resize(dot);
    if (name.starts_with("T-"))
        name.replace(0, 2, "M-");

    const std::string materialRel = folder + name + ".smat";
    const std::filesystem::path file = std::filesystem::path(source->Root) / materialRel;

    std::error_code ec;
    if (std::filesystem::exists(file, ec))
    {
        std::fprintf(stderr, "[shudei] '%s' already exists; opening it\n",
                     file.string().c_str());
    }
    else
    {
        MaterialDescription description;
        description.BaseColorTexture = AssetRef{ AssetType::Texture, textureVirtualPath };
        std::string error;
        if (!SaveMaterialFile(file.string(), description, &error))
        {
            std::fprintf(stderr, "[shudei] create material failed: %s\n", error.c_str());
            return;
        }
        RescanMaterials();
    }
    OpenMaterial("asset://" + materialRel);
}

bool MaterialEditorServices::RecookTexture(const TextureSourceLocation& source, std::string* error)
{
    if (!TextureRecook)
    {
        if (error != nullptr)
            *error = "no project mounted";
        return false;
    }
    for (const auto& entry : TextureRecook->Roots)
    {
        if (entry->Root != source.Root)
            continue;
        // Recook is synchronous; the resident swap commits at the engine's
        // async drain (the bindless slot repoints, so every material sampling
        // the texture follows within a frame).
        entry->Reloader.ReloadSource(source.RelPath);
        return true;
    }
    if (error != nullptr)
        *error = "'" + source.Root + "' is not a mounted content root";
    return false;
}

void MaterialEditorServices::RescanMaterials()
{
    if (!Assets || !Project)
        return;
    // Registry re-scan picks up files created since startup (this app's New/
    // Duplicate/Rename included), then the pickable list follows.
    for (const std::string& root : Project->ContentRoots)
        ScanAssetsDirectory(root, Assets->Registry, Assets->Assets.Kinds());
    Materials->Rescan(Project->ContentRoots);
}

void MaterialEditorServices::ApplyWorkingToResident(MaterialEditTab& tab)
{
    if (!Assets || !tab.Session.HasOpen() || !tab.Handle.IsValid())
        return;
    const AssetRecord* record = Assets->Registry.FindByPath(tab.Session.VirtualPath());
    if (record == nullptr)
        return;

    AssetStaging staging;
    staging.Record = *record;
    staging.Payload = tab.Session.Working();
    (void)Assets->Assets.MaterialLoaderRef().CommitReload(std::move(staging));
}

void MaterialEditorServices::UpdateTitle()
{
    MaterialEditTab* tab = Tabs.Active();
    std::string title = "Shudei - Material Editor";
    if (tab != nullptr && tab->Session.HasOpen())
    {
        title += " - ";
        title += tab->Session.VirtualPath();
        if (tab->Session.IsDirty())
            title += " *";
    }
    if (title != LastWindowTitle && Window != nullptr)
    {
        Window->SetTitle(title);
        LastWindowTitle = title;
    }
}
