#include "PieDriver.h"

#include "project/Project.h"

#include <app/Engine.h>
#include <core/console/ConsoleRegistry.h>
#include <core/logging/Logger.h>

#include <SDL3/SDL.h>

#include <filesystem>
#include <optional>
#include <span>
#include <system_error>

PieDriver::PieDriver(Engine& engine, WorldDocument& world, ProjectDescriptor* project, RuntimeAssets* assets)
    : Engine_(engine)
    , World_(world)
    , Project_(project)
    , Assets_(assets)
{
}

bool PieDriver::PreparePlay(const std::string& map,
                            std::vector<std::string>& startupArgs,
                            std::string& label,
                            std::string& appPath)
{
    Logger& log = Engine_.Logging().GetLogger<PieDriver>();

    if (Project_ == nullptr)
    {
        log.Error("play: no project open (set SENCHA_PROJECT)");
        return false;
    }

    // An explicit map plays single-zone; otherwise the last cook decides:
    // a cooked world launches the streaming path from the focus zone.
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
        return false;
    }

    appPath = ResolveHostAppPath();

    // Preflight the two paths fork/exec cannot report before it is too late: a
    // missing host binary and a missing game module both otherwise leave the
    // child dead at exit 127 with the editor reporting a phantom "session
    // started". Resolve the module the way the child will (relative to the
    // project directory it chdir's into).
    std::error_code ec;
    if (!std::filesystem::exists(appPath, ec))
    {
        log.Error("play failed: host app not found at '{}' (build the 'app' "
                  "target for this build tree)", appPath);
        return false;
    }
    const std::filesystem::path modulePath =
        std::filesystem::path(Project_->GameModulePath).is_absolute()
            ? std::filesystem::path(Project_->GameModulePath)
            : std::filesystem::path(Project_->Directory) / Project_->GameModulePath;
    if (!std::filesystem::exists(modulePath, ec))
    {
        log.Error("play failed: game module not found at '{}' (build the "
                  "project's game module)", modulePath.generic_string());
        return false;
    }

    return true;
}

void PieDriver::Play(const std::string& map)
{
    Logger& log = Engine_.Logging().GetLogger<PieDriver>();

    std::vector<std::string> startupArgs;
    std::string label;
    std::string app;
    if (!PreparePlay(map, startupArgs, label, app))
        return;

    // CWD is the project directory: the game resolves its content roots
    // ("assets", "assets/.cooked") relative to it, exactly as a shipped game.
    std::string commandLine;
    for (const std::string& arg : startupArgs)
        commandLine += " " + arg;
    log.Info("play: {} --game {}{} (cwd {})",
             app, Project_->GameModulePath, commandLine, Project_->Directory);

    std::string error;
    if (!Pie.Launch(app, Project_->GameModulePath, Project_->Directory, startupArgs, &error))
        log.Error("play failed: " + error);
    else
        log.Info("play: session started (" + label + ")");
}

void PieDriver::PlayHosted(const std::string& map)
{
    Logger& log = Engine_.Logging().GetLogger<PieDriver>();

    std::vector<std::string> startupArgs;
    std::string label;
    std::string app;
    if (!PreparePlay(map, startupArgs, label, app))
        return;

    const std::string port = std::to_string(HostPort);

    // The authority: the same content, no window, nobody playing in it. Its
    // output goes to the terminal the editor was started from, which is where
    // a server's log belongs.
    std::vector<std::string> hostArgs = startupArgs;
    hostArgs.push_back("--headless");
    hostArgs.push_back("+host");
    hostArgs.push_back(port);

    std::string error;
    if (!HostPie.Launch(app, Project_->GameModulePath, Project_->Directory, hostArgs, &error))
    {
        log.Error("play failed: host " + error);
        return;
    }

    // The player: no map of its own. It is told which world to load by the
    // authority when it is admitted, which is the same path a client run from
    // a bundle takes.
    const std::vector<std::string> clientArgs{ "+connect", "127.0.0.1:" + port };
    if (!Pie.Launch(app, Project_->GameModulePath, Project_->Directory, clientArgs, &error))
    {
        HostPie.Stop();
        log.Error("play failed: client " + error);
        return;
    }

    log.Info("play: hosted session started (" + label + " on port " + port + ")");
}

void PieDriver::Stop()
{
    // Client first, so the authority observes a peer leaving rather than
    // losing the socket underneath it -- which is also the path worth
    // exercising every time someone stops a test session.
    Pie.Stop();
    HostPie.Stop();
}

bool PieDriver::IsPlaying()
{
    Logger& log = Engine_.Logging().GetLogger<PieDriver>();

    const bool clientRunning = Pie.IsRunning();
    const bool hostRunning = HostPie.IsRunning();

    if (std::optional<std::string> report = Pie.TakeExitReport())
        log.Error("play: " + *report);
    if (std::optional<std::string> report = HostPie.TakeExitReport())
        log.Error("play: host " + *report);

    // A hosted session whose authority died leaves a client connected to
    // nothing; stopping the survivor keeps "playing" from meaning half a
    // session.
    if (hostRunning && !clientRunning)
        HostPie.Stop();

    return clientRunning || hostRunning;
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

    ConsoleCommandMetadata playHosted;
    playHosted.Name = "playserver";
    playHosted.Owner = "editor";
    playHosted.Usage = "playserver [map]";
    playHosted.Help = "Launch the project as a dedicated host plus a client joined "
                      "to it; map defaults to the last cooked level.";
    playHosted.Callback = [this](ConsoleExecutionContext&, std::span<const std::string> args) {
        PlayHosted(args.empty() ? LastCookedMap_ : args.front());
        return ConsoleResult{};
    };
    registry.RegisterCommand(std::move(playHosted));

    registry.RegisterCVar({
        .Name = "editor.pie.host_port",
        .Owner = "editor",
        .Type = CVarType::Int,
        .DefaultValue = static_cast<std::int64_t>(HostPort),
        .CurrentValue = static_cast<std::int64_t>(HostPort),
        .Flags = CVarFlags::Archive,
        .Help = "UDP port a hosted play session's authority binds.",
        .Source = { "editor" },
        .Min = 1.0,
        .Max = 65535.0,
        .OnChange = [this](const CVarChangeContext& ctx) {
            HostPort = static_cast<int>(std::get<std::int64_t>(ctx.NewValue));
        },
    });

    ConsoleCommandMetadata stop;
    stop.Name = "stop";
    stop.Owner = "editor";
    stop.Usage = "stop";
    stop.Help = "Stop the running PIE session.";
    stop.Callback = [this](ConsoleExecutionContext&, std::span<const std::string>) {
        ConsoleResult result;
        // Either half counts: a hosted session whose client has already been
        // closed still leaves an authority to shut down.
        if (!IsPlaying())
        {
            result.Info("no play session running");
            return result;
        }
        Stop();
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
