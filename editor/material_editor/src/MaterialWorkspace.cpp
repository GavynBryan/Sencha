#include "MaterialWorkspace.h"

#include "MaterialPreviewRenderFeature.h"
#include "TexturesPanel.h"

#include "actions/ActionOffer.h"
#include "presenters/ContentPicture.h"
#include "project/MaterialLibraryExplorer.h"
#include "properties/SettingProperties.h"
#include "ui/MaterialThumbnailCache.h"
#include "workspaces/WorkspaceWindow.h"

#include "documents/DocumentSourceSet.h"
#include "project/MaterialLibrary.h"
#include "project/Project.h"
#include "render/RenderFeatureDetach.h"
#include "ui/EditorThemeFile.h"
#include "ui/ImGuiTextureBinding.h"

#include <app/Engine.h>
#include <app/RuntimeContent.h>
#include <assets/cook/AssetImporter.h>
#include <assets/cook/TextureImportSettings.h>
#include <assets/hotreload/SourceReloadRoots.h>
#include <assets/material/MaterialAssetLoader.h>
#include <assets/material/MaterialWriter.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <core/console/ConsoleTypes.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/Renderer.h>
#include <app/GameContexts.h>
#include <ui/UiService.h>

#include <SDL3/SDL_events.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace
{
// The preview feature's teardown releases ImGui texture bindings through the
// backend the window's UI feature owns.
constexpr std::string_view kPreviewFeatureId = "material_preview";
constexpr std::array<std::string_view, 1> kPreviewDependsOn{ "editor_ui" };

struct PrimitiveRow
{
    PreviewPrimitive Primitive;
    const char* Id;
    const char* Label;
};
constexpr std::array<PrimitiveRow, 3> kPrimitives = { {
    { PreviewPrimitive::Sphere, "preview.sphere", "Sphere" },
    { PreviewPrimitive::Cube, "preview.cube", "Cube" },
    { PreviewPrimitive::Plane, "preview.plane", "Plane" },
} };

// Where the open material lives, as the one property that moves it.
class MaterialFileProperty final : public IPropertySource
{
public:
    MaterialFileProperty(std::function<std::string()> path, std::function<void(const std::string&)> move)
        : Path(std::move(path))
        , Move(std::move(move))
    {
    }

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override
    {
        const std::string path = Path();
        if (path.empty())
            return {};
        return { TextProperty("path", "File", path.substr(path.find("://") == std::string::npos ? 0 : path.find("://") + 3),
                              "Where it lives in its content root; changing it moves the file") };
    }
    bool BeginEdit(std::string_view key) override { return key == "path" && !Path().empty(); }
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Unchanged; }
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override
    {
        if (key != "path" || value.empty())
            return PropertyEditResult::Refused;
        Move(std::string(value));
        return PropertyEditResult::Applied;
    }
    void CancelEdit(std::string_view) override {}

private:
    std::function<std::string()> Path;
    std::function<void(const std::string&)> Move;
};
} // namespace

MaterialWorkspace::MaterialWorkspace(Engine& engine,
                                     const ProjectDescriptor* project,
                                     MaterialLibrary& materials,
                                     DocumentSourceSet& documents,
                                     PromptQueue& prompts,
                                     ActionCatalog& catalog)
    : EngineRef(engine)
    , Project(project)
    , Assets(engine.Content().Assets())
    , Materials(materials)
    , Documents(documents)
    , Tabs(documents, [this](MaterialEditTab& tab, const MaterialDescription& description) {
        PushToResident(tab, description);
    })
    , Prompts(prompts.MakeAsker())
    , Properties(Tabs, engine.Content().Assets().Registry)
{
    FilePath = std::make_unique<MaterialFileProperty>(
        [this] {
            const MaterialEditTab* tab = Tabs.Active();
            return tab != nullptr && tab->Session.HasOpen() ? tab->Session.VirtualPath() : std::string{};
        },
        [this](const std::string& path) {
            if (const MaterialEditTab* tab = Tabs.Active())
                RenameMaterial(tab->Session.VirtualPath(), path);
        });
    RegisterPreviewBackdropCVars();
    MaterialThumbnailCache::RegisterBudget(engine.Console().Registry());
    BuildUi();
    DefineActions(catalog);
}

MaterialWorkspace::~MaterialWorkspace()
{
    // The preview feature borrows the tabs' resident materials and the asset
    // stack, and the textures panel's bindings free their sets through the
    // live UI backend; both go before the panels and the tabs.
    DetachRenderFeature(EngineRef, Preview);
    if (Textures != nullptr)
        Textures->ReleasePreviewResources();
    Surface = WorkspaceView{};
    Picture.reset();
    Thumbnails.reset();
}

void MaterialWorkspace::BuildUi()
{
    Renderer& renderer = EngineRef.Graphics().MainRenderer;
    Preview = renderer.StageFeature(std::make_unique<MaterialPreviewRenderFeature>(Assets),
                                    FeatureRegistration{ .Id = kPreviewFeatureId,
                                                         .DependsOn = kPreviewDependsOn });
    std::vector<std::string_view> failed;
    if (!renderer.CommitStagedFeatures(&failed) || !failed.empty())
    {
        std::fprintf(stderr, "[materials] preview render feature failed to set up; "
                             "the preview is unavailable\n");
        Preview = nullptr;
    }
    if (UiService* ui = EngineRef.TryUi(); ui != nullptr && ui->IsReady())
    {
        Picture = std::make_unique<ContentPicture>(*ui, "material_preview");
        Thumbnails = std::make_unique<MaterialThumbnailCache>(
            Assets.Assets, *Assets.Textures, EngineRef.Graphics().Images, EngineRef.Graphics().Samplers,
            Assets.Registry, ui, &renderer.GetUiTextures(), "materials-workspace:");
    }

    Surface.File.Save = [this] { SaveActiveMaterial(); };

    auto texturesPanel = std::make_unique<TexturesPanel>(
        Assets.Registry,
        Project != nullptr ? Project->ContentRoots : std::vector<std::string>{},
        [this](const TextureSourceLocation& source, std::string* error)
        { return RecookTexture(source, error); },
        std::make_unique<ImGuiTextureBinding>(
            Assets.Assets, *Assets.Textures,
            EngineRef.Graphics().Images, EngineRef.Graphics().Samplers),
        [this](const std::string& textureVirtualPath)
        { CreateMaterialFromTexture(textureVirtualPath); });
    Textures = texturesPanel.get();
    Surface.AddPanel(std::move(texturesPanel));
}

void MaterialWorkspace::DefineActions(ActionCatalog& catalog)
{
    const auto open = [this] {
        const MaterialEditTab* tab = Tabs.Active();
        return tab != nullptr && tab->Session.HasOpen();
    };
    const bool canCreate = Project != nullptr && !Project->ContentRoots.empty();
    OfferAction(catalog, ActionHandlers,
                { .Id = "material.new", .Label = "New Material", .Category = "File",
                  .DefaultChords = { { SDLK_N, { .Ctrl = true } } } },
                { .Invoke = [this] { CreateMaterial(UnusedMaterialName("NewMaterial"), false); },
                  .Enabled = [canCreate] { return canCreate; } });
    OfferAction(catalog, ActionHandlers, { .Id = "material.duplicate", .Label = "Duplicate Material", .Category = "File" },
                { .Invoke = [this] {
                      if (const MaterialEditTab* tab = Tabs.Active())
                          CreateMaterial(UnusedMaterialName(std::string(MaterialLeafName(tab->Session.VirtualPath())) + "_copy"),
                                         true);
                  },
                  .Enabled = [open, canCreate] { return canCreate && open(); } });
    OfferAction(catalog, ActionHandlers,
                { .Id = "material.close", .Label = "Close Material", .Category = "File",
                  .DefaultChords = { { SDLK_W, { .Ctrl = true } } } },
                { .Invoke = [this] { CloseTab(Tabs.ActiveIndex()); }, .Enabled = open });
    OfferAction(catalog, ActionHandlers, MaterialExplorerAction(), {});
    OfferAction(catalog, ActionHandlers, MaterialRescanAction(), { .Invoke = [this] {
                    RescanMaterials();
                    if (Thumbnails != nullptr)
                        Thumbnails->Clear();
                } });
    for (const PrimitiveRow& row : kPrimitives)
        OfferAction(catalog, ActionHandlers, { .Id = row.Id, .Label = row.Label, .Category = "View" },
                    { .Invoke = [this, primitive = row.Primitive] {
                          if (Preview != nullptr)
                              Preview->SetPrimitive(primitive);
                      },
                      .Checked = [this, primitive = row.Primitive] {
                          return Preview != nullptr && Preview->GetPrimitive() == primitive;
                      } });
    OfferAction(catalog, ActionHandlers, { .Id = "preview.brighter", .Label = "Brighter Light", .Category = "View" },
                { .Invoke = [this] {
                    if (Preview != nullptr)
                        Preview->LightIntensity = std::min(40.0f, Preview->LightIntensity * 1.25f);
                } });
    OfferAction(catalog, ActionHandlers, { .Id = "preview.dimmer", .Label = "Dimmer Light", .Category = "View" },
                { .Invoke = [this] {
                    if (Preview != nullptr)
                        Preview->LightIntensity = std::max(0.5f, Preview->LightIntensity / 1.25f);
                } });
}

std::string MaterialWorkspace::UnusedMaterialName(const std::string& stem) const
{
    if (Project == nullptr || Project->ContentRoots.empty())
        return stem;
    const std::filesystem::path folder = std::filesystem::path(Project->ContentRoots.front()) / "materials";
    std::error_code ec;
    std::string name = stem;
    for (int n = 2; std::filesystem::exists(folder / (name + ".smat"), ec); ++n)
        name = stem + std::to_string(n);
    return name;
}

void MaterialWorkspace::RegisterPreviewBackdropCVars()
{
    ConsoleRegistry& registry = EngineRef.Console().Registry();
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

void MaterialWorkspace::UpdatePreviewBackdropStyle()
{
    if (Preview == nullptr)
        return;
    ConsoleRegistry& registry = EngineRef.Console().Registry();
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

void MaterialWorkspace::Tick(FrameUpdateContext&)
{
    for (const auto& tab : Tabs.Tabs())
        if (tab->Session.Version() != tab->AppliedVersion)
        {
            PushToResident(*tab, tab->Session.Working());
            tab->AppliedVersion = tab->Session.Version();
        }

    // The preview follows the active material, however it became active.
    MaterialEditTab* active = Tabs.Active();
    if (Preview != nullptr)
        Preview->SetMaterial(active != nullptr ? active->Handle() : MaterialHandle{});
    UpdatePreviewBackdropStyle();

    if (Picture != nullptr)
    {
        const Rect2d content = Visible ? ContentRect : Rect2d{};
        Picture->Place(content);
        if (Preview != nullptr && content.Width() >= 8.0f && content.Height() >= 8.0f)
            Preview->Show(VkExtent2D{ static_cast<std::uint32_t>(content.Width()),
                                      static_cast<std::uint32_t>(content.Height()) },
                          Picture->Image());
    }
    if (Thumbnails != nullptr)
        Thumbnails->UpdateFrame(EngineRef);
}

void MaterialWorkspace::SetVisible(bool visible)
{
    Visible = visible;
    if (!visible)
        Orbiting = false;
    if (Picture != nullptr)
        Picture->SetShown(visible);
}

void MaterialWorkspace::MoveTo(const WorkspaceWindow& window)
{
    if (Picture != nullptr)
        Picture->MoveTo(window.Content);
}

void MaterialWorkspace::HandlePlatformEvent(PlatformEventContext& ctx)
{
    if (Preview == nullptr || ctx.Handled)
        return;
    const SDL_Event& event = ctx.Event;
    UiService* ui = EngineRef.TryUi();
    const auto onStage = [&](float x, float y, std::uint32_t window) {
        const bool overUi = ui != nullptr && ui->Capture(window).Mouse;
        return !overUi && ContentRect.Contains(Vec2d(x, y));
    };
    switch (event.type)
    {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event.button.button == SDL_BUTTON_LEFT && onStage(event.button.x, event.button.y, event.button.windowID))
        {
            Orbiting = true;
            ctx.Handled = true;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button == SDL_BUTTON_LEFT && Orbiting)
        {
            Orbiting = false;
            ctx.Handled = true;
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        // Turntable feel: drag right orbits right, drag up orbits above.
        if (Orbiting)
        {
            Preview->Orbit(event.motion.xrel * 0.01f, -event.motion.yrel * 0.01f);
            ctx.Handled = true;
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (onStage(event.wheel.mouse_x, event.wheel.mouse_y, event.wheel.windowID))
        {
            Preview->Zoom(event.wheel.y);
            ctx.Handled = true;
        }
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        Orbiting = false;
        break;
    default:
        break;
    }
}

bool MaterialWorkspace::OwnsDocument(const DocumentRef& document) const
{
    return document.Source == &Tabs;
}

void MaterialWorkspace::OpenMaterial(const std::string& virtualPath)
{
    const AssetRecord* record = Assets.Registry.FindByPath(virtualPath);
    if (record == nullptr || record->FilePath.empty())
    {
        std::fprintf(stderr, "[materials] '%s' is not an editable material file\n", virtualPath.c_str());
        return;
    }

    std::string error;
    const bool existed = Tabs.Find(virtualPath) != nullptr;
    MaterialEditTab* tab = Tabs.OpenOrFocus(virtualPath, record->FilePath, &error);
    if (tab == nullptr)
    {
        std::fprintf(stderr, "[materials] failed to open '%s': %s\n", virtualPath.c_str(), error.c_str());
        return;
    }

    if (!existed)
    {
        tab->Resident = Assets.Assets.LoadLease(virtualPath, AssetType::Material);
        // The resident material just loaded from disk, which is the saved
        // (and, right after Open, working) state.
        tab->AppliedVersion = tab->Session.Version();
    }
}

void MaterialWorkspace::CloseTab(std::size_t index)
{
    if (index >= Tabs.Tabs().size())
        return;
    const MaterialEditTab& tab = *Tabs.Tabs()[index];
    const std::string path = tab.Session.VirtualPath();
    // By path, not index: the tabs may move while the question is open.
    AskAboutChanges(Prompts, tab.Session.IsDirty(), path, [this, path](DirtyDisposition disposition) {
        const auto& tabs = Tabs.Tabs();
        for (std::size_t i = 0; i < tabs.size(); ++i)
        {
            if (tabs[i]->Session.VirtualPath() != path)
                continue;
            std::string error;
            if (!Tabs.Close(i, disposition, error))
                std::fprintf(stderr, "[materials] %s\n", error.c_str());
            return;
        }
    });
}

void MaterialWorkspace::SaveActiveMaterial()
{
    MaterialEditTab* tab = Tabs.Active();
    if (tab == nullptr || !tab->Session.HasOpen())
        return;
    const DocumentSaveResult result = Documents.Save(Tabs.RefOf(*tab));
    if (result.Status == DocumentSaveStatus::Conflict)
        std::fprintf(stderr, "[materials] '%s' changed on disk since it was read\n", tab->Session.VirtualPath().c_str());
    else if (result.Status == DocumentSaveStatus::Failed)
        std::fprintf(stderr, "[materials] save failed: %s\n", result.Error.c_str());
}

void MaterialWorkspace::CreateMaterial(const std::string& name, bool duplicateOpen)
{
    if (Project == nullptr || Project->ContentRoots.empty() || name.empty())
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
        std::fprintf(stderr, "[materials] '%s' already exists\n", file.string().c_str());
        return;
    }

    std::string error;
    const bool written = duplicateOpen ? active->Session.SaveTo(file.string(), &error)
                                       : MaterialEditSession::CreateNew(file.string(), &error);
    if (!written)
    {
        std::fprintf(stderr, "[materials] create failed: %s\n", error.c_str());
        return;
    }

    RescanMaterials();
    OpenMaterial("asset://materials/" + name + ".smat");
}

void MaterialWorkspace::RenameMaterial(const std::string& virtualPath,
                                            const std::string& newRelPath)
{
    if (Project == nullptr || newRelPath.empty())
        return;
    const AssetRecord* record = Assets.Registry.FindByPath(virtualPath);
    if (record == nullptr || record->FilePath.empty())
    {
        std::fprintf(stderr, "[materials] '%s' is not a renameable material file\n", virtualPath.c_str());
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
        std::fprintf(stderr, "[materials] '%s' is outside every content root\n", record->FilePath.c_str());
        return;
    }

    std::string rel = newRelPath;
    if (rel.size() < 5 || rel.substr(rel.size() - 5) != ".smat")
        rel += ".smat";
    const std::filesystem::path newFile = (owningRoot / rel).lexically_normal();

    std::error_code ec;
    if (std::filesystem::exists(newFile, ec))
    {
        std::fprintf(stderr, "[materials] '%s' already exists\n", newFile.string().c_str());
        return;
    }
    std::filesystem::create_directories(newFile.parent_path(), ec);
    std::filesystem::rename(oldFile, newFile, ec);
    if (ec)
    {
        std::fprintf(stderr, "[materials] rename failed: %s\n", ec.message().c_str());
        return;
    }

    const std::string newVirtual = "asset://" + rel;
    // Levels referencing the old path are not rewritten; those faces render
    // as the level default until reassigned. Same policy as deleting a file.
    std::fprintf(stderr, "[materials] renamed '%s' -> '%s' (level refs are not rewritten)\n",
                 virtualPath.c_str(), newVirtual.c_str());

    if (MaterialEditTab* tab = Tabs.Find(virtualPath))
    {
        Tabs.Renamed(*tab, newVirtual, newFile.string());
        tab->Resident.Reset();
    }

    RescanMaterials();

    if (MaterialEditTab* tab = Tabs.Find(newVirtual); tab != nullptr && !tab->Resident.IsValid())
    {
        tab->Resident = Assets.Assets.LoadLease(newVirtual, AssetType::Material);
        // Force a re-apply so an unsaved working state survives the move.
        tab->AppliedVersion = 0;
    }
}

void MaterialWorkspace::CreateMaterialFromTexture(const std::string& textureVirtualPath)
{
    if (Project == nullptr)
        return;

    // "asset://textures/T-cliff.png" -> "textures/M-cliff.smat": beside the
    // texture, in the content root that owns its source file.
    const auto source = ResolveTextureSource(Project->ContentRoots, textureVirtualPath);
    if (!source)
    {
        std::fprintf(stderr, "[materials] '%s' has no source file under any content root\n",
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
        std::fprintf(stderr, "[materials] '%s' already exists; opening it\n",
                     file.string().c_str());
    }
    else
    {
        MaterialDescription description;
        description.BaseColorTexture = AssetRef{ AssetType::Texture, textureVirtualPath };
        std::string error;
        if (!SaveMaterialFile(file.string(), description, &error))
        {
            std::fprintf(stderr, "[materials] create material failed: %s\n", error.c_str());
            return;
        }
        RescanMaterials();
    }
    OpenMaterial("asset://" + materialRel);
}

bool MaterialWorkspace::RecookTexture(const TextureSourceLocation& source, std::string* error)
{
    SourceReloadRoots* sources = EngineRef.Content().SourceReload();
    // Recook is synchronous; the resident swap commits at the engine's async
    // drain (the bindless slot repoints, so every material sampling the
    // texture follows within a frame).
    if (sources != nullptr && sources->ReloadSource(source.Root, source.RelPath))
        return true;
    if (error != nullptr)
        *error = "'" + source.Root + "' is not a watched content root";
    return false;
}

void MaterialWorkspace::RescanMaterials()
{
    if (Project == nullptr)
        return;
    // Registry re-scan picks up files created since startup (this workspace's
    // New/Duplicate/Rename included), then the pickable list follows.
    for (const std::string& root : Project->ContentRoots)
        ScanAssetsDirectory(root, Assets.Registry, Assets.Assets.Kinds());
    Materials.Rescan(Project->ContentRoots);
}

void MaterialWorkspace::PushToResident(MaterialEditTab& tab, const MaterialDescription& description)
{
    if (!tab.Session.HasOpen() || !tab.Resident.IsValid())
        return;
    const AssetRecord* record = Assets.Registry.FindByPath(tab.Session.VirtualPath());
    if (record == nullptr)
        return;

    AssetStaging staging;
    staging.Record = *record;
    staging.Payload = description;
    (void)Assets.Assets.Reload(std::move(staging));
}

WorkspaceDocumentFacet MaterialWorkspace::Document() const
{
    WorkspaceDocumentFacet facet;
    for (const std::unique_ptr<MaterialEditTab>& tab : Tabs.Tabs())
        facet.Open.push_back({ .Key = tab->Session.VirtualPath(),
                               .Label = std::string(MaterialLeafName(tab->Session.VirtualPath())),
                               .Changed = tab->Session.IsDirty() });
    const MaterialEditTab* active = Tabs.Active();
    if (active == nullptr || !active->Session.HasOpen())
        return facet;
    facet.Label = std::string(MaterialLeafName(active->Session.VirtualPath()));
    facet.Changed = active->Session.IsDirty();
    facet.Active = active->Session.VirtualPath();
    return facet;
}

void MaterialWorkspace::ShowDocument(std::string_view key)
{
    for (std::size_t i = 0; i < Tabs.Tabs().size(); ++i)
        if (Tabs.Tabs()[i]->Session.VirtualPath() == key)
            Tabs.SetActive(i);
}

WorkspaceViewFacet MaterialWorkspace::Viewing() const
{
    if (Preview == nullptr)
        return {};
    return { .Projection = std::string(PreviewPrimitiveName(Preview->GetPrimitive())), .Views = 1 };
}

WorkspaceSelectionFacet MaterialWorkspace::Selection()
{
    const MaterialEditTab* tab = Tabs.Active();
    if (tab == nullptr || !tab->Session.HasOpen())
        return {};
    WorkspaceSelectionFacet facet;
    facet.Title = std::string(MaterialLeafName(tab->Session.VirtualPath()));
    facet.Actions = { "file.save", "material.duplicate", "material.close" };
    facet.Sections.push_back({ .Heading = {}, .Source = FilePath.get() });
    facet.Sections.push_back({ .Heading = {}, .Source = &Properties });
    return facet;
}

std::vector<WorkspaceExplorerInfo> MaterialWorkspace::Explorers() const
{
    return { MaterialExplorerInfo() };
}

ExplorerPage MaterialWorkspace::Explore(const ExplorerQuery& query)
{
    if (query.Explorer != kMaterialExplorerId)
        return {};
    const MaterialEditTab* active = Tabs.Active();
    ExplorerPage page = MaterialLibraryPage(
        Materials, query, active != nullptr ? std::string_view(active->Session.VirtualPath()) : std::string_view{},
        [this](const std::string& path) { return Thumbnails != nullptr ? Thumbnails->ImageName(path) : std::string{}; });
    page.Actions.insert(page.Actions.begin(), "material.new");
    return page;
}

void MaterialWorkspace::ChooseInExplorer(std::string_view explorer, std::string_view key, ExplorerChoice, std::size_t)
{
    if (explorer == kMaterialExplorerId)
        OpenMaterial(std::string(key));
}
