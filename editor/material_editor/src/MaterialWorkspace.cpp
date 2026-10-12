#include "MaterialWorkspace.h"

#include "MaterialPreviewRenderFeature.h"
#include "project/ProjectFileSteps.h"
#include "project/ProjectFiles.h"

#include "actions/ActionOffer.h"
#include "presenters/ContentPicture.h"
#include "problems/ProblemList.h"
#include "project/MaterialLibraryActions.h"
#include "properties/FilePathProperty.h"
#include "properties/SettingProperties.h"
#include "text/TextFilterMatch.h"
#include "ui/MaterialThumbnailCache.h"
#include "workspaces/WorkspaceWindow.h"

#include "documents/DocumentSourceSet.h"
#include "project/MaterialLibrary.h"
#include "project/Project.h"
#include "render/RenderFeatureDetach.h"
#include "ui/EditorThemeFile.h"

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
#include <filesystem>
#include <format>
#include <optional>
#include <memory>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace
{
constexpr std::string_view kPreviewFeatureId = "material_preview";
constexpr std::string_view kProblemSource = "materials";

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


} // namespace

ActionDefinition NewMaterialAction()
{
    return { .Id = "material.new", .Label = "New Material", .Category = "File",
             .DefaultChords = { { SDLK_N, { .Ctrl = true } } } };
}

MaterialWorkspace::MaterialWorkspace(Engine& engine,
                                     const ProjectDescriptor* project,
                                     MaterialLibrary& materials,
                                     DocumentSourceSet& documents,
                                     ProjectFileSteps* files,
                                     PromptQueue& prompts,
                                     ActionCatalog& catalog,
                                     ProblemList& problems,
                                     AssetPictures& pictures)
    : EngineRef(engine)
    , Project(project)
    , Assets(engine.Content().Assets())
    , Materials(materials)
    , Documents(documents)
    , Files(files)
    , Tabs(documents, [this](MaterialEditTab& tab, const MaterialDescription& description) {
        PushToResident(tab, description);
    })
    , Prompts(prompts.MakeAsker())
    , Problems(problems)
    , Properties(Tabs, engine.Content().Assets().Registry)
    , TextureImport(
          [this](const std::string& texture, const TextureImportSettings& settings, std::string& cooked,
                 std::string& error) {
              const std::optional<TextureSourceLocation> source =
                  Project != nullptr ? ResolveTextureSource(Project->ContentRoots, texture) : std::nullopt;
              if (!source.has_value())
              {
                  error = "No source file under any content root";
                  return false;
              }
              if (!SaveTextureImportSettingsFor(*source, settings, &error) || !RecookTexture(*source, &error))
                  return false;
              // What the cook wrote, read back rather than assumed.
              cooked = DescribeCookedTextureState(ReadCookedTextureState(*source));
              return true;
          },
          [this](const std::string& texture) {
              CreateMaterialFromTexture(texture);
              ShowMaterial();
          })
    , Pictures(pictures)
{
    FilePath = std::make_unique<FilePathProperty>(
        [this] {
            const MaterialEditTab* tab = Tabs.Active();
            return tab != nullptr && tab->Session.HasOpen() ? tab->Session.VirtualPath() : std::string{};
        },
        [this](const std::string& path) {
            // The open tab follows its file, changes and history and all.
            const MaterialEditTab* tab = Tabs.Active();
            if (tab == nullptr || Files == nullptr)
                return;
            Report(Files->Move(tab->Session.VirtualPath(), path));
        });
    RegisterPreviewBackdropCVars();
    BuildUi();
    DefineActions(catalog);
    OfferAction(catalog, ActionHandlers, SaveAction(), { .Invoke = [this] { SaveActiveMaterial(); } });
}

MaterialWorkspace::~MaterialWorkspace()
{
    Problems.Clear(kProblemSource);
    // The preview feature borrows the tabs' resident materials and the asset
    // stack, so it goes before the tabs.
    DetachRenderFeature(EngineRef, Preview);
    Picture.reset();
}

void MaterialWorkspace::BuildUi()
{
    Preview = AttachRenderFeature(EngineRef, std::make_unique<MaterialPreviewRenderFeature>(Assets), kPreviewFeatureId);
    if (Preview == nullptr)
        SetupProblem = "The material preview did not set up, so the stage will not draw.";
    if (UiService* ui = EngineRef.TryUi(); ui != nullptr && ui->IsReady())
        Picture = std::make_unique<ContentPicture>(*ui, "material_preview");
    Report({});
}

void MaterialWorkspace::Report(std::string error)
{
    std::vector<Problem> problems;
    if (!SetupProblem.empty())
        problems.push_back({ .Severity = ProblemSeverity::Error, .Message = SetupProblem });
    if (!error.empty())
        problems.push_back({ .Severity = ProblemSeverity::Warning, .Message = std::move(error) });
    Problems.Publish(kProblemSource, std::move(problems));
}

void MaterialWorkspace::DefineActions(ActionCatalog& catalog)
{
    const auto open = [this] {
        const MaterialEditTab* tab = Tabs.Active();
        return tab != nullptr && tab->Session.HasOpen();
    };
    const bool canCreate = Project != nullptr && !Project->ContentRoots.empty();
    OfferAction(catalog, ActionHandlers, NewMaterialAction(),
                { .Invoke = [this] { CreateMaterial(UnusedMaterialName("NewMaterial")); },
                  .Enabled = [canCreate] { return canCreate; } });
    OfferAction(catalog, ActionHandlers, { .Id = "material.duplicate", .Label = "Duplicate Material", .Category = "File" },
                { .Invoke = [this] { DuplicateActive(); }, .Enabled = [this, open] { return Files != nullptr && open(); } });
    OfferAction(catalog, ActionHandlers,
                { .Id = "material.close", .Label = "Close Material", .Category = "File",
                  .DefaultChords = { { SDLK_W, { .Ctrl = true } } } },
                { .Invoke = [this] { CloseTab(Tabs.ActiveIndex()); }, .Enabled = open });
    OfferAction(catalog, ActionHandlers, MaterialRescanAction(), { .Invoke = [this] {
                    RescanMaterials();
                    Pictures.Forget();
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
        if (Preview != nullptr && CanShowStage(content))
            Preview->Show(VkExtent2D{ static_cast<std::uint32_t>(content.Width()),
                                      static_cast<std::uint32_t>(content.Height()) },
                          Picture->Image());
    }
}

void MaterialWorkspace::SetVisible(bool visible)
{
    Visible = visible;
    if (!visible)
        Orbit.Cancel();
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
    const auto onStage = [&](float x, float y) { return ContentRect.Contains(Vec2d(x, y)); };
    switch (event.type)
    {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event.button.button == SDL_BUTTON_LEFT && onStage(event.button.x, event.button.y))
        {
            Orbit.Press(Vec2d(event.button.x, event.button.y));
            ctx.Handled = true;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button == SDL_BUTTON_LEFT && Orbit.IsHeld())
        {
            (void)Orbit.Release();
            ctx.Handled = true;
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (const std::optional<StageOrbitDrag::Turn> turn =
                Orbit.Move(Vec2d(event.motion.x, event.motion.y), Vec2d(event.motion.xrel, event.motion.yrel)))
        {
            Preview->Orbit(turn->Yaw, turn->Pitch);
            ctx.Handled = true;
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (onStage(event.wheel.mouse_x, event.wheel.mouse_y))
        {
            Preview->Zoom(event.wheel.y);
            ctx.Handled = true;
        }
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        Orbit.Cancel();
        break;
    default:
        break;
    }
}

bool MaterialWorkspace::OwnsDocument(const DocumentRef& document) const
{
    return document.Source == &Tabs;
}

bool MaterialWorkspace::OpenMaterial(const std::string& virtualPath)
{
    const AssetRecord* record = Assets.Registry.FindByPath(virtualPath);
    if (record == nullptr || record->FilePath.empty())
    {
        Report(std::format("{} is not an editable material file.", virtualPath));
        return false;
    }

    std::string error;
    const bool existed = Tabs.Find(virtualPath) != nullptr;
    MaterialEditTab* tab = Tabs.OpenOrFocus(virtualPath, record->FilePath, &error);
    if (tab == nullptr)
    {
        Report(std::format("{} did not open: {}", virtualPath, error));
        return false;
    }
    Report({});

    if (!existed)
    {
        tab->Resident = Assets.Assets.LoadLease(virtualPath, AssetType::Material);
        // The resident material just loaded from disk, which is the saved
        // (and, right after Open, working) state.
        tab->AppliedVersion = tab->Session.Version();
    }
    return true;
}

void MaterialWorkspace::CloseTab(std::size_t index)
{
    if (index >= Tabs.Tabs().size())
        return;
    MaterialEditTab& tab = *Tabs.Tabs()[index];
    const std::string path = tab.Session.VirtualPath();
    std::vector<DocumentRef> changed;
    if (tab.Session.IsDirty())
        changed.push_back(Tabs.RefOf(tab));
    // By path, not index: the tabs may move while the question is open.
    AskAboutDocuments(Prompts, "Close " + std::string(MaterialLeafName(path)), changed,
                      [this, path](DirtyDisposition disposition) {
                          const auto& tabs = Tabs.Tabs();
                          for (std::size_t i = 0; i < tabs.size(); ++i)
                          {
                              if (tabs[i]->Session.VirtualPath() != path)
                                  continue;
                              std::string error;
                              (void)Tabs.Close(i, disposition, error);
                              Report(std::move(error));
                              return;
                          }
                      });
}

void MaterialWorkspace::SaveActiveMaterial()
{
    MaterialEditTab* tab = Tabs.Active();
    if (tab == nullptr || !tab->Session.HasOpen())
        return;
    // A conflict or a failure is the save follow-up's to report.
    (void)Documents.Save(Tabs.RefOf(*tab));
}

void MaterialWorkspace::CreateMaterial(const std::string& name)
{
    if (Project == nullptr || Project->ContentRoots.empty() || name.empty())
        return;

    const std::filesystem::path root(Project->ContentRoots.front());
    const std::filesystem::path file = root / "materials" / (name + ".smat");
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    if (std::filesystem::exists(file, ec))
    {
        Report(std::format("{} already exists.", file.generic_string()));
        return;
    }

    std::string error;
    if (!MaterialEditSession::CreateNew(file.string(), &error))
    {
        Report(std::format("The material was not created: {}", error));
        return;
    }

    RescanMaterials();
    (void)OpenMaterial("asset://materials/" + name + ".smat");
}

void MaterialWorkspace::DuplicateActive()
{
    const MaterialEditTab* tab = Tabs.Active();
    if (tab == nullptr || !tab->Session.HasOpen() || Files == nullptr)
        return;
    const std::string copy = CopyPath(tab->Session.VirtualPath(),
                                      [this](const std::string& path) { return Assets.Registry.Contains(path); });
    if (std::string refused = Files->Duplicate(tab->Session.VirtualPath(), copy); !refused.empty())
    {
        Report(std::move(refused));
        return;
    }
    (void)OpenMaterial(copy);
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
        Report(std::format("{} has no source file under any content root.", textureVirtualPath));
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
    if (!std::filesystem::exists(file, ec))
    {
        MaterialDescription description;
        description.BaseColorTexture = Assets.Registry.Current(AssetRef{ AssetType::Texture, textureVirtualPath });
        std::string error;
        if (!SaveMaterialFile(file.string(), description, &error))
        {
            Report(std::format("The material was not created: {}", error));
            return;
        }
        RescanMaterials();
    }
    (void)OpenMaterial("asset://" + materialRel);
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
    ShowMaterial();
}

void MaterialWorkspace::PickTexture(const std::string& texturePath)
{
    const std::optional<TextureSourceLocation> source =
        Project != nullptr ? ResolveTextureSource(Project->ContentRoots, texturePath) : std::nullopt;
    TextureImportProperties::Shown shown{ .Texture = texturePath, .Settings = std::nullopt, .Cooked = "No source file" };
    if (source.has_value())
    {
        std::string error;
        shown.Settings = LoadTextureImportSettingsFor(*source, &error);
        shown.Cooked = DescribeCookedTextureState(ReadCookedTextureState(*source));
        if (!error.empty())
            shown.Problem = "The settings file is unreadable; these are the defaults: " + error;
    }
    TextureImport.Show(std::move(shown));
    Subject = CardSubject::Texture;
}

void MaterialWorkspace::ShowMaterial()
{
    Subject = CardSubject::Material;
}

WorkspaceViewFacet MaterialWorkspace::Viewing() const
{
    if (Preview == nullptr)
        return {};
    return { .Projection = std::string(PreviewPrimitiveName(Preview->GetPrimitive())), .Views = 1 };
}

WorkspaceSelectionFacet MaterialWorkspace::Selection()
{
    if (Subject == CardSubject::Texture && !TextureImport.Texture().empty())
    {
        const std::string& texture = TextureImport.Texture();
        return { .Title = texture.substr(texture.rfind('/') + 1),
                 .Sections = { { .Heading = {}, .Source = &TextureImport } } };
    }
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

bool MaterialWorkspace::OpenAsset(const ProjectFile& file)
{
    if (file.Extension == ".smat")
    {
        if (!OpenMaterial(file.AssetPath))
            return false;
        ShowMaterial();
        return true;
    }
    PickTexture(file.AssetPath);
    return true;
}
