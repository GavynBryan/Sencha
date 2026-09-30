#include "ProjectWorkspace.h"

#include "ProjectRack.h"
#include "ProjectRelaunch.h"

#include "actions/ActionOffer.h"
#include "project/ProcessLaunch.h"
#include "project/Project.h"
#include "workspaces/WorkspaceWindow.h"

#include <SDL3/SDL.h>

#include <app/Engine.h>
#include <platform/SdlWindow.h>
#include <ui/UiService.h>

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

ProjectWorkspace::ProjectWorkspace(Engine& engine, SdlWindow& window, ActionCatalog& catalog, std::string executable)
    : EngineRef(engine)
    , Window(window)
    , Executable(std::move(executable))
    , Settings([](const ProjectDescriptor& descriptor, std::string& error) {
        ProjectDescriptor saved = descriptor;
        return saved.Save(descriptor.Directory + "/project.senchaproj", &error);
    })
    , Creation(ListTemplates(), [this](const std::string& directory, const std::string& name,
                                       const std::string& templateName, std::string& error) {
        return CreateProject(directory, name, templateName, error);
    })
{
    std::string error;
    if (!Catalog.Load(ProjectCatalog::DefaultCatalogPath(), &error))
        std::fprintf(stderr, "[projects] %s (starting with an empty list)\n", error.c_str());
    if (UiService* ui = engine.TryUi(); ui != nullptr && ui->IsReady())
        Rack = std::make_unique<ProjectRack>(*ui);
    DefineActions(catalog);
}

ProjectWorkspace::~ProjectWorkspace() = default;

void ProjectWorkspace::DefineActions(ActionCatalog& catalog)
{
    OfferAction(catalog, ActionHandlers,
                { .Id = "project.new", .Label = "New Project", .Category = "File",
                  .DefaultChords = { { SDLK_N, { .Ctrl = true } } } },
                { .Invoke = [this] { BeginNewProject(); } });
    OfferAction(catalog, ActionHandlers,
                { .Id = "project.browse", .Label = "Open Project File...", .Category = "File",
                  .DefaultChords = { { SDLK_O, { .Ctrl = true } } } },
                { .Invoke = [this] { BrowseForProject(); } });
    OfferAction(catalog, ActionHandlers, { .Id = "project.open", .Label = "Open Project", .Category = "File" },
                { .Invoke = [this] { OpenProject(Picked); },
                  .Enabled = [this] { return !Creating && !Picked.empty() && Settings.Shown().has_value(); } });
    OfferAction(catalog, ActionHandlers, { .Id = "project.forget", .Label = "Forget", .Category = "File" },
                { .Invoke = [this] { RemoveCatalogEntry(Picked); },
                  .Enabled = [this] { return !Creating && !Picked.empty(); } });
}

void ProjectWorkspace::Tick(FrameUpdateContext&)
{
    std::vector<std::string> browsed;
    {
        const std::scoped_lock lock(PendingMutex);
        browsed.swap(PendingBrowsedProjects);
    }
    for (const std::string& path : browsed)
    {
        TouchCatalog(path);
        Pick(Catalog.Entries().empty() ? std::string{} : Catalog.Entries().front().Path);
    }

    if (Rack == nullptr)
        return;
    for (const RackRequest& request : Rack->Update(Catalog, Creating ? std::string{} : Picked, Creating))
    {
        switch (request.Choice)
        {
        case RackChoice::Pick:   Pick(request.Path); break;
        case RackChoice::Open:   OpenProject(request.Path); break;
        case RackChoice::Forget: RemoveCatalogEntry(request.Path); break;
        case RackChoice::New:    BeginNewProject(); break;
        case RackChoice::Browse: BrowseForProject(); break;
        }
    }
}

void ProjectWorkspace::SetVisible(bool visible)
{
    if (Rack != nullptr)
        Rack->SetVisible(visible);
}

void ProjectWorkspace::MoveTo(const WorkspaceWindow& window)
{
    if (Rack != nullptr)
        Rack->MoveTo(window.Interactive);
}

WorkspaceSelectionFacet ProjectWorkspace::Selection()
{
    if (Creating)
        return { .Title = "New Project", .Sections = { { .Heading = {}, .Source = &Creation } } };
    if (Picked.empty())
        return {};
    const std::optional<ProjectDescriptor>& shown = Settings.Shown();
    WorkspaceSelectionFacet facet;
    facet.Title = shown.has_value() && !shown->Name.empty() ? shown->Name
                                                            : std::filesystem::path(Picked).parent_path().filename().string();
    facet.Actions = { "project.open", "project.forget" };
    facet.Sections.push_back({ .Heading = {}, .Source = &Settings });
    return facet;
}

void ProjectWorkspace::BeginNewProject()
{
    Creating = true;
    Creation.Clear();
}

void ProjectWorkspace::Pick(const std::string& projectPath)
{
    Creating = false;
    Picked = projectPath;
    if (projectPath.empty())
    {
        Settings.Show(std::nullopt);
        return;
    }
    ProjectDescriptor descriptor;
    std::string error;
    if (ProjectDescriptor::Load(projectPath, descriptor, &error))
        Settings.Show(std::move(descriptor));
    else
        Settings.Show(std::nullopt, error);
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

bool ProjectWorkspace::CreateProject(const std::string& directory, const std::string& name,
                                     const std::string& templateName, std::string& error)
{
    ProjectDescriptor descriptor;
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
        return false;

    const std::string path =
        (std::filesystem::path(directory) / "project.senchaproj").lexically_normal().string();
    Catalog.Touch(path, descriptor.Name);
    SaveCatalog();
    Pick(path);
    return true;
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
    if (projectPath == Picked)
        Pick({});
}

void ProjectWorkspace::SaveCatalog()
{
    std::string error;
    if (!Catalog.Save(ProjectCatalog::DefaultCatalogPath(), &error))
        std::fprintf(stderr, "[projects] %s\n", error.c_str());
}
