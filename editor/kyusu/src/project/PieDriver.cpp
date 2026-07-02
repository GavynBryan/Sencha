#include "PieDriver.h"

#include "project/Project.h"
#include "document/DocumentCook.h"
#include "document/EditorDocument.h"

#include <app/Engine.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <core/console/ConsoleTypes.h>
#include <core/logging/Logger.h>

#include <SDL3/SDL.h>

#include <filesystem>
#include <span>
#include <variant>

PieDriver::PieDriver(Engine& engine, EditorDocument& document, ProjectDescriptor* project, RuntimeAssets* assets)
    : Engine_(engine)
    , Document_(document)
    , Project_(project)
    , Assets_(assets)
{
}

std::string PieDriver::Cook(const std::string& levelName)
{
    Logger& log = Engine_.Logging().GetLogger<PieDriver>();
    if (Project_ == nullptr)
    {
        log.Error("cook: no project open (set SENCHA_PROJECT)");
        return {};
    }

    // Name the artifacts after the explicit arg, else the document's file stem,
    // else "untitled" for a never-saved level.
    std::string name = levelName;
    if (name.empty())
    {
        const std::filesystem::path docPath(Document_.GetDisplayName());
        name = Document_.HasFilePath() ? docPath.stem().string() : "untitled";
    }

    double cellSize = 16.0;
    if (const CVarMetadata* cvar = Engine_.Console().Registry().FindCVar("editor.cook.cell_size");
        cvar != nullptr && std::holds_alternative<double>(cvar->CurrentValue))
        cellSize = std::get<double>(cvar->CurrentValue);

    if (Assets_ == nullptr)
    {
        log.Error("cook: asset system not initialized");
        return {};
    }

    const std::filesystem::path assetsRoot = std::filesystem::path(Project_->Directory) / "assets";
    const DocumentCookResult cooked =
        CookDocument(Document_, name, assetsRoot, cellSize, Engine_.Logging(), Assets_);
    if (!cooked.Success)
    {
        log.Error("cook failed: " + cooked.Error);
        return {};
    }

    LastCookedMap_ = "levels/" + name;
    log.Info("cooked '{}' ({} cells) -> {}",
             LastCookedMap_, cooked.CellCount, cooked.CookedScenePath.generic_string());
    return LastCookedMap_;
}

void PieDriver::Play(const std::string& map)
{
    Logger& log = Engine_.Logging().GetLogger<PieDriver>();

    if (Project_ == nullptr)
    {
        log.Error("play: no project open (set SENCHA_PROJECT)");
        return;
    }
    if (map.empty())
    {
        log.Error("play: no cooked map; cook a level first or pass a map name");
        return;
    }

    const std::string app = ResolveHostAppPath();
    // CWD is the project directory: the game resolves its content roots
    // ("assets", "assets/.cooked") relative to it, exactly as a shipped game.
    log.Info("play: {} --game {} +map {} (cwd {})",
             app, Project_->GameModulePath, map, Project_->Directory);

    std::string error;
    if (!Pie.Launch(app, Project_->GameModulePath, Project_->Directory, map, &error))
        log.Error("play failed: " + error);
    else
        log.Info("play: session started (" + map + ")");
}

void PieDriver::Stop()
{
    Pie.Stop();
}

bool PieDriver::IsPlaying()
{
    return Pie.IsRunning();
}

std::string PieDriver::ResolveHostAppPath() const
{
    const char* base = SDL_GetBasePath();
    if (base == nullptr)
        return "app";

    // weakly_canonical drops SDL's trailing slash so parent_path is the real parent.
    const std::filesystem::path baseDir = std::filesystem::weakly_canonical(base);

    // Installed SDK: app sits beside the editor (bin/app, bin/kyusu).
    std::filesystem::path candidate = baseDir / "app";
    if (std::filesystem::exists(candidate))
        return candidate.string();

    // Build tree: the editor is build/editor/kyusu/, app is build/app/.
    candidate = baseDir.parent_path().parent_path() / "app" / "app";
    if (std::filesystem::exists(candidate))
        return candidate.string();

    return (baseDir / "app").string();
}

void PieDriver::RegisterCommands(ConsoleRegistry& registry)
{
    registry.RegisterCVar({
        .Name = "editor.cook.cell_size",
        .Owner = "editor",
        .Type = CVarType::Double,
        .DefaultValue = 16.0,
        .CurrentValue = 16.0,
        .Flags = CVarFlags::Archive,
        .Help = "World-space grid size the level cook clusters brushes into per-cell meshes.",
        .Source = { "editor" },
        .Min = 0.0,
    });

    ConsoleCommandMetadata cook;
    cook.Name = "cook";
    cook.Owner = "editor";
    cook.Usage = "cook [name]";
    cook.Help = "Cook the live level into the project's assets (name defaults to the document).";
    cook.Callback = [this](ConsoleExecutionContext&, std::span<const std::string> args) {
        ConsoleResult result;
        const std::string name = args.empty() ? std::string{} : args.front();
        const std::string map = Cook(name);
        if (map.empty())
            result.Error("cook failed (see log)");
        else
            result.Info("cooked " + map);
        return result;
    };
    registry.RegisterCommand(std::move(cook));

    ConsoleCommandMetadata play;
    play.Name = "play";
    play.Owner = "editor";
    play.Usage = "play [map]";
    play.Help = "Launch the project in the app host (PIE); map defaults to the last cooked level.";
    play.Callback = [this](ConsoleExecutionContext&, std::span<const std::string> args) {
        Play(args.empty() ? LastCookedMap_ : args.front());
        return ConsoleResult{};
    };
    registry.RegisterCommand(std::move(play));

    ConsoleCommandMetadata stop;
    stop.Name = "stop";
    stop.Owner = "editor";
    stop.Usage = "stop";
    stop.Help = "Stop the running PIE session.";
    stop.Callback = [this](ConsoleExecutionContext&, std::span<const std::string>) {
        ConsoleResult result;
        if (!Pie.IsRunning())
        {
            result.Info("no play session running");
            return result;
        }
        Pie.Stop();
        result.Info("stopped play session");
        return result;
    };
    registry.RegisterCommand(std::move(stop));

    ConsoleCommandMetadata project;
    project.Name = "project";
    project.Owner = "editor";
    project.Usage = "project <info|save|new <dir> [name]>";
    project.Help = "Inspect, save, or create a project descriptor (.senchaproj).";
    project.Callback = [this](ConsoleExecutionContext&, std::span<const std::string> args) {
        ConsoleResult result;
        const std::string verb = args.empty() ? "info" : args.front();
        if (verb == "info")
        {
            if (Project_ == nullptr)
                result.Info("no project open (set SENCHA_PROJECT)");
            else
                result.Info("project '" + Project_->Name + "' @ " + Project_->Directory);
        }
        else if (verb == "save")
        {
            if (Project_ == nullptr)
            {
                result.Error("no project open");
                return result;
            }
            std::string error;
            const std::string path =
                (std::filesystem::path(Project_->Directory) / "project.senchaproj").string();
            if (!Project_->Save(path, &error))
                result.Error("save failed: " + error);
            else
                result.Info("saved " + path);
        }
        else if (verb == "new")
        {
            if (args.size() < 2)
            {
                result.Error("usage: project new <dir> [name]");
                return result;
            }
            ProjectDescriptor created;
            std::string error;
            const std::string name = args.size() >= 3 ? args[2] : std::string{};
            if (!ProjectDescriptor::Create(args[1], name, created, &error))
                result.Error("create failed: " + error);
            else
                result.Info("created project at " + created.Directory);
        }
        else
        {
            result.Error("unknown verb '" + verb + "'");
        }
        return result;
    };
    registry.RegisterCommand(std::move(project));
}
