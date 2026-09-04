#include <app/LevelCommands.h>

#include <app/Engine.h>
#include <app/LoadedLevel.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <math/geometry/3d/Transform3d.h>
#include <runtime/spawn/SceneSpawnService.h>
#include <zone/ZoneId.h>

#include <exception>
#include <optional>
#include <span>
#include <string>

namespace
{
[[nodiscard]] ConsoleResult Usage(std::string text)
{
    ConsoleResult result;
    result.Status = ConsoleStatus::InvalidArguments;
    result.Error(std::move(text));
    return result;
}
} // namespace

void RegisterLevelCommands(ConsoleService& console, Engine& engine)
{
    ConsoleRegistry& registry = console.Registry();

    registry.RegisterCommand({
        .Name = "map",
        .Owner = "engine",
        .Usage = "map <path>",
        .Help = "Load a cooked scene as the play map.",
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [&console, &engine](ConsoleExecutionContext&,
                                        std::span<const std::string> args) {
            if (args.size() != 1)
                return Usage("usage: map <path>");
            ConsoleResult result = engine.Level().LoadScene(args[0]);
            if (result.Status == ConsoleStatus::Ok)
                console.NoteLoadedMap(args[0]);
            return result;
        },
    });

    registry.RegisterCommand({
        .Name = "world",
        .Owner = "engine",
        .Usage = "world <name>",
        .Help = "Load a cooked partitioned world and stream its zones.",
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [&console, &engine](ConsoleExecutionContext&,
                                        std::span<const std::string> args) {
            if (args.size() != 1)
                return Usage("usage: world <name>");
            // Focus follows the manifest's start zone. A game that knows
            // better -- a saved position -- calls LoadWorld itself.
            ConsoleResult result = engine.Level().LoadWorld(args[0], std::nullopt);
            if (result.Status == ConsoleStatus::Ok)
                console.NoteLoadedMap(args[0]);
            return result;
        },
    });

    registry.RegisterCommand({
        .Name = "zone",
        .Owner = "engine",
        .Usage = "zone <16-hex zone id>",
        .Help = "Focus the loaded world on a zone.",
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [&engine](ConsoleExecutionContext&,
                              std::span<const std::string> args) {
            if (args.size() != 1)
                return Usage("usage: zone <16-hex zone id>");
            const std::optional<ZoneId> zone = ZoneIdFromString(args[0]);
            if (!zone.has_value())
                return Usage("malformed zone id '" + args[0] + "'");
            return engine.Level().FocusZone(*zone);
        },
    });

    registry.RegisterCommand({
        .Name = "zones",
        .Owner = "engine",
        .Usage = "zones",
        .Help = "Print world partition demand and residency.",
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [&engine](ConsoleExecutionContext&,
                              std::span<const std::string>) {
            return engine.Level().DescribeZones();
        },
    });

    registry.RegisterCommand({
        .Name = "scene.spawn",
        .Owner = "engine",
        .Usage = "scene.spawn <asset://...smap> [x y z]",
        .Help = "Spawn a cooked scene at the given position (origin by default).",
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [&engine](ConsoleExecutionContext&,
                              std::span<const std::string> args) {
            ConsoleResult result;
            if (args.size() != 1 && args.size() != 4)
                return Usage("usage: scene.spawn <asset://...smap> [x y z]");
            Transform3f root = Transform3f::Identity();
            if (args.size() == 4)
            {
                try
                {
                    root.Position = Vec3d(std::stof(args[1]), std::stof(args[2]),
                                          std::stof(args[3]));
                }
                catch (const std::exception&)
                {
                    return Usage("scene.spawn: position must be three numbers");
                }
            }
            const SceneSpawnId id = engine.Spawns().RequestSpawn(args[0], root);
            result.Info("spawn " + std::to_string(id.Value) + " requested ("
                        + SceneSpawnStatusName(engine.Spawns().Status(id)) + ")");
            return result;
        },
    });

    registry.RegisterCommand({
        .Name = "scene.despawn",
        .Owner = "engine",
        .Usage = "scene.despawn <spawn id>",
        .Help = "Destroy a live scene spawn's entities.",
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [&engine](ConsoleExecutionContext&,
                              std::span<const std::string> args) {
            ConsoleResult result;
            if (args.size() != 1)
                return Usage("usage: scene.despawn <spawn id>");
            SceneSpawnId id{};
            try
            {
                id.Value = std::stoull(args[0]);
            }
            catch (const std::exception&)
            {
                return Usage("scene.despawn: id must be a number");
            }
            if (engine.Spawns().RequestDespawn(id))
                result.Info("despawn queued");
            else
                result.Error("spawn " + std::to_string(id.Value) + " is "
                             + SceneSpawnStatusName(engine.Spawns().Status(id)));
            return result;
        },
    });
}
