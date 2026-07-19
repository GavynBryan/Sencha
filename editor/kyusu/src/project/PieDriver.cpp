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
#include <core/logging/Logger.h>
#include <zone/WorldPartitionIds.h>

#include <SDL3/SDL.h>

#include <filesystem>
#include <optional>
#include <span>
#include <system_error>
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

    const auto readDouble = [this](const char* name, double fallback) {
        const CVarMetadata* cvar = Engine_.Console().Registry().FindCVar(name);
        if (cvar != nullptr && std::holds_alternative<double>(cvar->CurrentValue))
            return std::get<double>(cvar->CurrentValue);
        return fallback;
    };
    const double cellSize = readDouble("editor.cook.cell_size", 16.0);

    // Lightmap tuning rides the cook so PIE always reflects the dialed values;
    // the diffuse wrap comes from the renderer style cvar so baked and dynamic
    // lighting share one shading model.
    LightingCookParams lightmapParams{};
    lightmapParams.Shading.DiffuseWrap = static_cast<float>(
        readDouble("render.style.diffuse_wrap", lightmapParams.Shading.DiffuseWrap));
    lightmapParams.LuxelSize = static_cast<float>(
        readDouble("editor.cook.lightmap_luxel", lightmapParams.LuxelSize));
    lightmapParams.MaxAtlasSize = static_cast<std::uint32_t>(
        readDouble("editor.cook.lightmap_max_size", lightmapParams.MaxAtlasSize));
    lightmapParams.ConeDegrees = static_cast<float>(
        readDouble("editor.cook.lightmap_cone", lightmapParams.ConeDegrees));
    lightmapParams.Probe.BounceAlbedo = static_cast<float>(
        readDouble("editor.cook.probe_albedo", lightmapParams.Probe.BounceAlbedo));
    // The probe bake replaces the runtime hemispheric ambient, so its miss
    // environment is that exact pair.
    lightmapParams.Probe.SkyColor = Vec3d(
        static_cast<float>(readDouble("render.ambient.sky_r", 0.10)),
        static_cast<float>(readDouble("render.ambient.sky_g", 0.12)),
        static_cast<float>(readDouble("render.ambient.sky_b", 0.15)));
    lightmapParams.Probe.GroundColor = Vec3d(
        static_cast<float>(readDouble("render.ambient.ground_r", 0.04)),
        static_cast<float>(readDouble("render.ambient.ground_g", 0.03)),
        static_cast<float>(readDouble("render.ambient.ground_b", 0.02)));

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
            CookWorld(World_, assetsRoot, cellSize, Engine_.Logging(), Assets_,
                      lightmapParams);
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
        CookDocument(World_.FocusDocument(), name, assetsRoot, cellSize, Engine_.Logging(), Assets_,
                     lightmapParams);
    if (!cooked.Success)
    {
        log.Error("cook failed: " + cooked.Error);
        return {};
    }

    LastCookedWorld_.clear();
    LastCookedZone_.clear();
    LastCookedMap_ = "levels/" + name;
    LastCook_ = CookRecord{ cooked.CookedScenePath, cooked.ContentHash,
                            LastCook_.Serial + 1 };
    log.Info("cooked '{}' ({} cells) -> {}",
             LastCookedMap_, cooked.CellCount, cooked.CookedScenePath.generic_string());
    if (cooked.DirectLightCount > 0)
        log.Info("cook: baked {} direct light(s) into a {}x{} lightmap atlas "
                 "(luxel {})",
                 cooked.DirectLightCount, cooked.LightmapAtlasWidth,
                 cooked.LightmapAtlasHeight, cooked.EffectiveLuxelSize);
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

    // Preflight the two paths fork/exec cannot report before it is too late: a
    // missing host binary and a missing game module both otherwise leave the
    // child dead at exit 127 with the editor reporting a phantom "session
    // started". Resolve the module the way the child will (relative to the
    // project directory it chdir's into).
    std::error_code ec;
    if (!std::filesystem::exists(app, ec))
    {
        log.Error("play failed: host app not found at '{}' (build the 'app' "
                  "target for this build tree)", app);
        return;
    }
    const std::filesystem::path modulePath =
        std::filesystem::path(Project_->GameModulePath).is_absolute()
            ? std::filesystem::path(Project_->GameModulePath)
            : std::filesystem::path(Project_->Directory) / Project_->GameModulePath;
    if (!std::filesystem::exists(modulePath, ec))
    {
        log.Error("play failed: game module not found at '{}' (build the "
                  "project's game module)", modulePath.generic_string());
        return;
    }

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

void PieDriver::Stop()
{
    Pie.Stop();
}

bool PieDriver::IsPlaying()
{
    const bool running = Pie.IsRunning();
    if (std::optional<std::string> report = Pie.TakeExitReport())
        Engine_.Logging().GetLogger<PieDriver>().Error("play: " + *report);
    return running;
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

    const auto registerBakeDouble = [&registry](const char* name, double defaultValue,
                                                const char* help, double min, double max) {
        registry.RegisterCVar({
            .Name = name,
            .Owner = "editor",
            .Type = CVarType::Double,
            .DefaultValue = defaultValue,
            .CurrentValue = defaultValue,
            .Flags = CVarFlags::Archive,
            .Help = help,
            .Source = { "editor" },
            .Min = min,
            .Max = max,
        });
    };
    registerBakeDouble("editor.cook.lightmap_luxel", 0.25,
                       "World units per lightmap texel. Smaller is finer; the atlas "
                       "density-clamps if the max size cannot hold it.", 0.05, 4.0);
    registerBakeDouble("editor.cook.lightmap_max_size", 2048.0,
                       "Per-dimension lightmap atlas cap. Keep at or below 4096 so "
                       "vertex UVs stay sub-texel accurate.", 128.0, 4096.0);
    registerBakeDouble("editor.cook.lightmap_cone", 45.0,
                       "Chart normal-cone split limit in degrees: soft-edged faces join "
                       "one lightmap chart while their normals stay inside this cone.",
                       5.0, 90.0);
    registerBakeDouble("editor.cook.probe_albedo", 0.35,
                       "Constant surface reflectance for the irradiance-probe bounce: "
                       "how much of a surface's direct lighting probes pick up.",
                       0.0, 1.0);

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
