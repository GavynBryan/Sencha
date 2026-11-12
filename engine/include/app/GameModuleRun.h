#pragma once

#include <app/Engine.h>
#include <app/GameModuleLoader.h>
#include <core/config/EngineConfig.h>
#include <core/console/ConsoleStartupScript.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

struct GameModuleRunSettings
{
    std::string Module{};
    std::string ContentRoot{};
    std::string Map{};
    std::uint64_t Frames = 0;
    // None keeps the rate the module configured.
    std::optional<double> TargetFps = 120.0;
    // Off keeps the window the module configured.
    bool Headless = true;
    // Console lines run before the map loads, and after it.
    std::vector<ConsoleCommandLine> Before{};
    std::vector<ConsoleCommandLine> After{};
    // Last word on the configuration, once the module has made its own.
    std::function<void(EngineConfig&)> Adjust{};
};

// Runs a built game module the way the host does: loaded through the real module
// path, configured by its own OnConfigure, on one map for a fixed number of frames. Register probes on Host()
// before Run. The engine outlives Run so its world can still be read, and the
// module outlives the engine, which holds the module's registrations until
// Shutdown.
class GameModuleRun
{
public:
    explicit GameModuleRun(const GameModuleRunSettings& settings);
    ~GameModuleRun();

    GameModuleRun(const GameModuleRun&) = delete;
    GameModuleRun& operator=(const GameModuleRun&) = delete;

    [[nodiscard]] bool Loaded() const { return Runtime.has_value(); }
    [[nodiscard]] const std::string& Error() const { return Problem; }
    [[nodiscard]] Engine& Host() { return *Runtime; }

    int Run();
    void Shutdown();

private:
    GameModuleLoader Loader;
    LoadedModule Module;
    std::optional<Engine> Runtime;
    std::string Problem;
};
