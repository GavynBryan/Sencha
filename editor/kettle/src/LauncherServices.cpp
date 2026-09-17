#include "LauncherServices.h"

#include "ProjectBrowserPanel.h"

#include "project/ProcessLaunch.h"
#include "project/Project.h"
#include "ui/EditorThemeStartup.h"
#include "ui/EditorUiFeature.h"

#include <SDL3/SDL.h>

#include <app/Engine.h>
#include <app/EngineSchedule.h>
#include <app/Game.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/Renderer.h>
#include <platform/SdlWindow.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <utility>

namespace
{
    constexpr SDL_DialogFileFilter kProjectFileFilters[] = {
        { "Sencha Project", "senchaproj" },
        { "All files", "*" },
    };

    class FrameHook
    {
    public:
        explicit FrameHook(std::function<void()> fn) : Fn(std::move(fn)) {}
        void FrameUpdate(FrameUpdateContext&) { if (Fn) Fn(); }

    private:
        std::function<void()> Fn;
    };
}

LauncherServices::LauncherServices(Engine& engine, SdlWindow& window)
{
    EnginePtr = &engine;
    Window = &window;

    std::string error;
    if (!Catalog.Load(ProjectCatalog::DefaultCatalogPath(), &error))
        std::fprintf(stderr, "[kettle] %s (starting with an empty list)\n", error.c_str());

    BuildUi();
}

LauncherServices::~LauncherServices() = default;

void LauncherServices::BuildUi()
{
    Engine& engine = *EnginePtr;
    ApplyEditorThemeFromConsole(engine.Console());

    auto uiFeature = std::make_unique<EditorUiFeature>(
        engine, *Window, engine.Graphics().Instance, engine.Graphics().Frames,
        "kettle.imgui.ini");
    UiFeature = uiFeature.get();

    auto browserPanel = std::make_unique<ProjectBrowserPanel>(
        Catalog,
        ProjectBrowserPanel::Actions{
            .OpenLevelEditor = [this](const std::string& path) { LaunchEditor("kyusu", path); },
            .OpenMaterialEditor = [this](const std::string& path) { LaunchEditor("shudei", path); },
            .OpenDataEditor = [this](const std::string& path) { LaunchEditor("data_editor", path); },
            .OpenUiPreviewer = [this](const std::string& path) { LaunchEditor("shoji", path); },
            .BrowseForProject = [this]() { BrowseForProject(); },
            .CreateProject = [this](const std::string& dir, const std::string& name,
                                    const std::string& templateName)
            { CreateProject(dir, name, templateName); },
            .RemoveEntry = [this](const std::string& path) { RemoveCatalogEntry(path); },
            .SettingsSaved = [this](const ProjectDescriptor& descriptor, const std::string& path)
            { TouchCatalog(path); (void)descriptor; },
        },
        ListTemplates());

    // File menu: New = the create-project modal, Open = browse for a
    // .senchaproj. Save/SaveAs stay unset (nothing document-like to save).
    ProjectBrowserPanel* browser = browserPanel.get();
    UiFeature->SetFileActions(
        [browser]() { browser->RequestCreateProject(); },
        [this]() { BrowseForProject(); },
        {},
        {});

    UiFeature->AddPanel(std::move(browserPanel));

    engine.Graphics().MainRenderer.AddFeature(std::move(uiFeature));
}

void LauncherServices::RegisterSystems(EngineSchedule& schedule)
{
    schedule.Register<FrameHook>([this] { ProcessFrame(); });
}

void LauncherServices::HandlePlatformEvent(PlatformEventContext& ctx)
{
    if (UiFeature != nullptr)
        UiFeature->ProcessSdlEvent(ctx.Event);
}

void LauncherServices::ProcessFrame()
{
    std::vector<std::string> browsed;
    {
        const std::scoped_lock lock(PendingMutex);
        browsed.swap(PendingBrowsedProjects);
    }
    for (const std::string& path : browsed)
        TouchCatalog(path);

    // Reap finished children so long launcher sessions do not accumulate
    // zombies. Children are deliberately not killed on launcher exit.
    ChildPids.erase(std::remove_if(ChildPids.begin(), ChildPids.end(), HasProcessExited),
                    ChildPids.end());
}

std::filesystem::path LauncherServices::ResolveTemplatesDirectory()
{
    const char* base = SDL_GetBasePath();
    if (base == nullptr)
        return {};
    const std::filesystem::path baseDir = std::filesystem::weakly_canonical(base);

    std::error_code ec;
    // Installed SDK: bin/kettle beside share/sencha/templates.
    std::filesystem::path candidate = baseDir.parent_path() / "share" / "sencha" / "templates";
    if (std::filesystem::is_directory(candidate, ec))
        return candidate;
    // In-tree: the launcher runs from its build directory somewhere under the
    // repository, whose templates/ carries the in-tree helper.
    for (std::filesystem::path dir = baseDir; !dir.empty() && dir != dir.root_path();
         dir = dir.parent_path())
    {
        candidate = dir / "templates";
        if (std::filesystem::is_regular_file(candidate / "InTreeTemplate.cmake", ec))
            return candidate;
    }
    return {};
}

std::vector<std::string> LauncherServices::ListTemplates()
{
    std::vector<std::string> names;
    const std::filesystem::path templates = ResolveTemplatesDirectory();
    if (templates.empty())
        return names;
    std::error_code ec;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(templates, ec))
    {
        if (entry.is_directory(ec)
            && std::filesystem::is_regular_file(entry.path() / "project.senchaproj", ec))
        {
            names.push_back(entry.path().filename().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string LauncherServices::ResolveEditorBinary(const char* name)
{
    const char* base = SDL_GetBasePath();
    if (base == nullptr)
        return name;

    // weakly_canonical drops SDL's trailing slash so parent_path is the real parent.
    const std::filesystem::path baseDir = std::filesystem::weakly_canonical(base);

    // Installed SDK: the editors sit side by side in bin/.
    std::filesystem::path candidate = baseDir / name;
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec))
        return candidate.string();

    // Build tree: build/editor/kettle/ next to build/editor/<name>/<name>.
    candidate = baseDir.parent_path() / name / name;
    if (std::filesystem::exists(candidate, ec))
        return candidate.string();

    return (baseDir / name).string();
}

void LauncherServices::LaunchEditor(const char* binaryName, const std::string& projectPath)
{
    const std::string binary = ResolveEditorBinary(binaryName);
    const std::string absolute =
        std::filesystem::absolute(std::filesystem::path(projectPath)).lexically_normal().string();

    long pid = -1;
    std::string error;
    if (!SpawnProcess(binary, { "--project", absolute }, std::string{}, pid, &error))
    {
        std::fprintf(stderr, "[kettle] failed to launch %s: %s\n", binaryName, error.c_str());
        return;
    }
    ChildPids.push_back(pid);
    std::fprintf(stderr, "[kettle] launched %s --project %s (pid %ld)\n",
                 binary.c_str(), absolute.c_str(), pid);
    TouchCatalog(projectPath);
}

void LauncherServices::BrowseForProject()
{
    if (Window == nullptr || Window->GetHandle() == nullptr)
        return;

    SDL_ShowOpenFileDialog(
        [](void* userdata, const char* const* filelist, int)
        {
            auto* self = static_cast<LauncherServices*>(userdata);
            if (filelist != nullptr && filelist[0] != nullptr)
            {
                const std::scoped_lock lock(self->PendingMutex);
                self->PendingBrowsedProjects.emplace_back(filelist[0]);
            }
        },
        this,
        Window->GetHandle(),
        kProjectFileFilters,
        static_cast<int>(std::size(kProjectFileFilters)),
        nullptr,
        false);
}

void LauncherServices::CreateProject(const std::string& directory, const std::string& name,
                                     const std::string& templateName)
{
    ProjectDescriptor descriptor;
    std::string error;
    bool created = false;
    if (templateName.empty())
    {
        created = ProjectDescriptor::Create(directory, name, descriptor, &error);
    }
    else
    {
        const std::filesystem::path templates = ResolveTemplatesDirectory();
        created = !templates.empty()
            && ProjectDescriptor::CreateFromTemplate(
                (templates / templateName).string(), directory, name, descriptor, &error);
        if (templates.empty())
            error = "no templates directory beside this launcher";
    }
    if (!created)
    {
        std::fprintf(stderr, "[kettle] create project failed: %s\n", error.c_str());
        return;
    }

    const std::string path =
        (std::filesystem::path(directory) / "project.senchaproj").lexically_normal().string();
    Catalog.Touch(path, descriptor.Name);
    SaveCatalog();
}

void LauncherServices::TouchCatalog(const std::string& projectPath)
{
    // One canonical form per project so a relative and an absolute spelling of
    // the same path cannot produce two rows.
    const std::string canonical =
        std::filesystem::absolute(std::filesystem::path(projectPath)).lexically_normal().string();

    // Read the descriptor for a display name; an unreadable file still lands in
    // the list (badged missing/broken in the UI) rather than vanishing.
    ProjectDescriptor descriptor;
    std::string error;
    std::string name;
    if (ProjectDescriptor::Load(canonical, descriptor, &error))
        name = descriptor.Name;
    Catalog.Touch(canonical, name);
    SaveCatalog();
}

void LauncherServices::RemoveCatalogEntry(const std::string& projectPath)
{
    Catalog.Remove(projectPath);
    SaveCatalog();
}

void LauncherServices::SaveCatalog()
{
    std::string error;
    if (!Catalog.Save(ProjectCatalog::DefaultCatalogPath(), &error))
        std::fprintf(stderr, "[kettle] %s\n", error.c_str());
}
