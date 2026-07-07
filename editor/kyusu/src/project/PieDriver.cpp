#include "PieDriver.h"

#include "project/Project.h"
#include "document/DocumentCook.h"
#include "document/EditorDocument.h"
#include "document/WorldCook.h"
#include "document/WorldDocument.h"

#include <app/Engine.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <core/console/ConsoleTypes.h>
#include <core/logging/LogLevel.h>
#include <core/logging/Logger.h>
#include <zone/WorldPartitionIds.h>

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <variant>

PieDriver::PieDriver(Engine& engine, WorldDocument& world, ProjectDescriptor* project, RuntimeAssets* assets)
    : Engine_(engine)
    , World_(world)
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

    // World mode cooks every saved zone through the world cook; Play then
    // launches the world path (+world +zone) against the cooked manifest.
    if (World_.IsWorld())
    {
        // The world cook reads authored files from disk and refuses dirty
        // documents (a stale cook would silently lie); saving here keeps
        // Cook a one-click action. A never-saved world still needs a path.
        if (World_.IsDirty())
        {
            if (!World_.HasSaveTarget())
            {
                log.Error("cook: save the world first (no file path yet)");
                return {};
            }
            if (!World_.SaveWorld())
            {
                log.Error("cook: saving the world before cook failed");
                return {};
            }
            log.Info("cook: saved the world");
        }

        const WorldCookResult cooked =
            CookWorld(World_, assetsRoot, cellSize, Engine_.Logging(), Assets_);
        if (!cooked.Success)
        {
            log.Error("cook failed: " + cooked.Error);
            return {};
        }

        LastCookedWorld_ = std::filesystem::path(std::string(World_.WorldPath())).stem().string();
        LastCookedZone_ = ZoneIdToString(World_.FocusZone());
        LastCookedMap_.clear();
        log.Info("cooked world ({} zones) -> {}", cooked.ZoneCount,
                 cooked.CookedManifestPath.generic_string());
        return LastCookedWorld_;
    }

    // Name the artifacts after the explicit arg, else the document's file stem,
    // else "untitled" for a never-saved level.
    std::string name = levelName;
    if (name.empty())
    {
        const std::filesystem::path docPath(World_.FocusDocument().GetDisplayName());
        name = World_.FocusDocument().HasFilePath() ? docPath.stem().string() : "untitled";
    }

    const DocumentCookResult cooked =
        CookDocument(World_.FocusDocument(), name, assetsRoot, cellSize, Engine_.Logging(), Assets_);
    if (!cooked.Success)
    {
        log.Error("cook failed: " + cooked.Error);
        return {};
    }

    LastCookedWorld_.clear();
    LastCookedZone_.clear();
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

    // An explicit map plays single-zone; otherwise the last cook decides:
    // a cooked world launches the streaming path from the focus zone.
    std::vector<std::string> startupArgs;
    std::string label;
    if (!map.empty())
    {
        startupArgs = { "+map", map };
        label = map;
    }
    else if (!LastCookedWorld_.empty())
    {
        startupArgs = { "+world", LastCookedWorld_, "+zone", LastCookedZone_ };
        label = LastCookedWorld_ + " (zone " + LastCookedZone_ + ")";
    }
    else
    {
        log.Error("play: nothing cooked; cook first or pass a map name");
        return;
    }

    const std::string app = ResolveHostAppPath();
    // The player mirrors its log into this file so the editor can tail it (the
    // player's stdout is the editor's own inherited stream). Reset the tail so we
    // read from the freshly truncated file.
    PieLogPath_ = (std::filesystem::path(Project_->Directory) / "pie.log").string();
    PieLogOffset_ = 0;
    PieLogPending_.clear();
    // CWD is the project directory: the game resolves its content roots
    // ("assets", "assets/.cooked") relative to it, exactly as a shipped game.
    std::string commandLine;
    for (const std::string& arg : startupArgs)
        commandLine += " " + arg;
    log.Info("play: {} --game {}{} (cwd {})",
             app, Project_->GameModulePath, commandLine, Project_->Directory);

    std::string error;
    if (!Pie.Launch(app, Project_->GameModulePath, Project_->Directory, PieLogPath_, startupArgs,
                    &error))
        log.Error("play failed: " + error);
    else
        log.Info("play: session started (" + label + ")");
}

void PieDriver::Stop()
{
    Pie.Stop();
}

bool PieDriver::IsPlaying()
{
    return Pie.IsRunning();
}

namespace
{
    // Category for lines mirrored from the PIE player, so the console shows their
    // origin distinctly from the editor's own PieDriver messages.
    struct PieLog
    {
    };

    LogLevel PieLineLevel(const std::string& line)
    {
        if (line.find("[ERROR]") != std::string::npos || line.find("[CRIT]") != std::string::npos)
            return LogLevel::Error;
        if (line.find("[WARN]") != std::string::npos)
            return LogLevel::Warning;
        return LogLevel::Info;
    }
}

void PieDriver::Poll()
{
    const bool playing = Pie.IsRunning(); // reaps and records the exit status

    if (playing || WasPlaying_)
        DrainPieLog(); // surface new player lines (including a crash's final ones)

    if (WasPlaying_ && !playing)
    {
        const PieExitStatus exit = Pie.TakeExit();
        Logger& log = Engine_.Logging().GetLogger<PieDriver>();
        if (exit.Kind == PieExitKind::Crashed)
            log.Error("PIE crashed: signal {} (the player aborted; see the log above)", exit.Value);
        else if (exit.Kind == PieExitKind::Exited && exit.Value != 0)
            log.Warn("PIE exited with code {}", exit.Value);
        else
            log.Info("PIE session ended");
    }
    WasPlaying_ = playing;
}

void PieDriver::DrainPieLog()
{
    if (PieLogPath_.empty())
        return;
    std::ifstream file(PieLogPath_, std::ios::binary);
    if (!file)
        return;
    file.seekg(0, std::ios::end);
    const std::streamoff end = file.tellg();
    if (end < PieLogOffset_) // the player truncated the file on (re)launch
        PieLogOffset_ = 0;
    if (end <= PieLogOffset_)
        return;

    std::string chunk(static_cast<std::size_t>(end - PieLogOffset_), '\0');
    file.seekg(PieLogOffset_, std::ios::beg);
    file.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    PieLogOffset_ = end;

    // Emit complete lines; carry any trailing partial line to the next drain.
    PieLogPending_ += chunk;
    Logger& log = Engine_.Logging().GetLogger<PieLog>();
    std::size_t newline;
    while ((newline = PieLogPending_.find('\n')) != std::string::npos)
    {
        const std::string line = PieLogPending_.substr(0, newline);
        PieLogPending_.erase(0, newline + 1);
        if (!line.empty())
            log.Log(PieLineLevel(line), line);
    }
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
