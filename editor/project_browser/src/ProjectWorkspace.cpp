#include "ProjectWorkspace.h"

#include "ProjectBrowserPanel.h"
#include "ProjectRelaunch.h"

#include "project/ProcessLaunch.h"
#include "project/Project.h"

#include <SDL3/SDL.h>

#include <app/Engine.h>
#include <platform/SdlWindow.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <utility>

namespace
{
    constexpr SDL_DialogFileFilter kProjectFileFilters[] = {
        { "Sencha Project", "senchaproj" },
        { "All files", "*" },
    };
}

ProjectWorkspace::ProjectWorkspace(Engine& engine, SdlWindow& window, std::string executable)
    : EngineRef(engine)
    , Window(window)
    , Executable(std::move(executable))
{
    std::string error;
    if (!Catalog.Load(ProjectCatalog::DefaultCatalogPath(), &error))
        std::fprintf(stderr, "[projects] %s (starting with an empty list)\n", error.c_str());

    auto browserPanel = std::make_unique<ProjectBrowserPanel>(
        Catalog,
        ProjectBrowserPanel::Actions{
            .OpenProject = [this](const std::string& path) { OpenProject(path); },
            .BrowseForProject = [this] { BrowseForProject(); },
            .CreateProject = [this](const std::string& dir, const std::string& name,
                                    const std::string& templateName)
            { CreateProject(dir, name, templateName); },
            .RemoveEntry = [this](const std::string& path) { RemoveCatalogEntry(path); },
            .SettingsSaved = [this](const ProjectDescriptor&, const std::string& path) { TouchCatalog(path); },
        },
        ListTemplates());

    // New is the create-project form and Open browses for a .senchaproj;
    // there is nothing document-like to save.
    ProjectBrowserPanel* browser = browserPanel.get();
    Surface.File.New = [browser] { browser->RequestCreateProject(); };
    Surface.File.Open = [this] { BrowseForProject(); };
    Surface.AddPanel(std::move(browserPanel));
}

ProjectWorkspace::~ProjectWorkspace() = default;

void ProjectWorkspace::Tick(FrameUpdateContext&)
{
    std::vector<std::string> browsed;
    {
        const std::scoped_lock lock(PendingMutex);
        browsed.swap(PendingBrowsedProjects);
    }
    for (const std::string& path : browsed)
        TouchCatalog(path);
}

void ProjectWorkspace::OpenProject(const std::string& projectPath)
{
    const char* base = SDL_GetBasePath();
    const ProcessCommand command = BuildProjectRelaunch(
        base != nullptr ? std::filesystem::path(base) : std::filesystem::path{}, Executable, projectPath);

    ChildProcess child;
    std::string error;
    if (!SpawnProcess(command.Binary, command.Args, std::string{}, child, &error))
    {
        std::fprintf(stderr, "[projects] failed to open %s: %s\n", projectPath.c_str(), error.c_str());
        return;
    }
    TouchCatalog(projectPath);
    // The new process owns the project from here. This one leaves through the
    // same gate as the window's close button, so nothing unsaved is lost.
    EngineRef.RequestExit();
}

std::filesystem::path ProjectWorkspace::ResolveTemplatesDirectory()
{
    const char* base = SDL_GetBasePath();
    if (base == nullptr)
        return {};
    const std::filesystem::path baseDir = std::filesystem::weakly_canonical(base);

    std::error_code ec;
    // Installed SDK: bin/ beside share/sencha/templates.
    std::filesystem::path candidate = baseDir.parent_path() / "share" / "sencha" / "templates";
    if (std::filesystem::is_directory(candidate, ec))
        return candidate;
    // In-tree: the application runs from its build directory somewhere under the
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

std::vector<std::string> ProjectWorkspace::ListTemplates()
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

void ProjectWorkspace::BrowseForProject()
{
    if (Window.GetHandle() == nullptr)
        return;

    SDL_ShowOpenFileDialog(
        [](void* userdata, const char* const* filelist, int)
        {
            auto* self = static_cast<ProjectWorkspace*>(userdata);
            if (filelist != nullptr && filelist[0] != nullptr)
            {
                const std::scoped_lock lock(self->PendingMutex);
                self->PendingBrowsedProjects.emplace_back(filelist[0]);
            }
        },
        this,
        Window.GetHandle(),
        kProjectFileFilters,
        static_cast<int>(std::size(kProjectFileFilters)),
        nullptr,
        false);
}

void ProjectWorkspace::CreateProject(const std::string& directory, const std::string& name,
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
            error = "no templates directory beside this application";
    }
    if (!created)
    {
        std::fprintf(stderr, "[projects] create project failed: %s\n", error.c_str());
        return;
    }

    const std::string path =
        (std::filesystem::path(directory) / "project.senchaproj").lexically_normal().string();
    Catalog.Touch(path, descriptor.Name);
    SaveCatalog();
}

void ProjectWorkspace::TouchCatalog(const std::string& projectPath)
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

void ProjectWorkspace::RemoveCatalogEntry(const std::string& projectPath)
{
    Catalog.Remove(projectPath);
    SaveCatalog();
}

void ProjectWorkspace::SaveCatalog()
{
    std::string error;
    if (!Catalog.Save(ProjectCatalog::DefaultCatalogPath(), &error))
        std::fprintf(stderr, "[projects] %s\n", error.c_str());
}
