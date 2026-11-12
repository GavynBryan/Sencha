#include <app/Application.h>
#include <app/Game.h>
#include <app/GameModuleLoader.h>
#include <core/config/EngineConfig.h>
#include <platform/UserPaths.h>

#include "HostArguments.h"

#include <SDL3/SDL.h>

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

//=============================================================================
// app: the Sencha runtime host.
//
// Pure glue: it loads a game module (which provides a Game), runs it, and
// unloads. It carries no per-project code; a game is a module + content, and
// the same binary is what the editor spawns for Play-In-Editor. The module is
// resolved as --game <path>, then $SENCHA_GAME_MODULE, then the default game
// module beside the executable (game.so / .dll / .dylib) -- so a bundle of
// app + game module runs with a bare `app +map levels/foo`. The host's own
// flags are in HostArguments; everything else (the +commands) flows through
// the engine's startup script.
//=============================================================================

namespace
{
    // Raised by a signal handler, read by the frame loop's exit predicate.
    //
    // Process-wide because signals are: this is the host catching them, not any
    // engine inside it. Ctrl-C at a server's terminal and the SIGTERM a service
    // manager sends on shutdown are how a dedicated host is asked to stop, and
    // both must land on the ordinary exit path so the world tears down and the
    // peers are told rather than the process simply vanishing.
    volatile std::sig_atomic_t HostExitRequested = 0;

    extern "C" void OnHostExitSignal(int) { HostExitRequested = 1; }

    void InstallHostExitSignals()
    {
        std::signal(SIGINT, OnHostExitSignal);
        std::signal(SIGTERM, OnHostExitSignal);
#if !defined(_WIN32)
        // A backgrounded host reading its terminal would otherwise be stopped
        // by the kernel the first time it tried.
        std::signal(SIGTTIN, SIG_IGN);
#endif
    }

    // Standard input, when the process has one at all -- a terminal, a pipe, or
    // a file all administer a host equally well. A host started as a service
    // with its input closed gets nothing to read, which is not an error: it is
    // simply administered by other means.
    int HostCommandDescriptor()
    {
#if defined(_WIN32)
        return -1;
#else
        return ::fcntl(STDIN_FILENO, F_GETFD) != -1 ? STDIN_FILENO : -1;
#endif
    }

#if defined(_WIN32)
    constexpr const char* kModuleExtension = ".dll";
#elif defined(__APPLE__)
    constexpr const char* kModuleExtension = ".dylib";
#else
    constexpr const char* kModuleExtension = ".so";
#endif

    // The default game module sits next to the executable as game<ext>: drop a
    // game module there and `app` runs it with no --game. Empty if none found.
    std::string DefaultModuleBesideExe()
    {
        const char* base = SDL_GetBasePath();
        if (base == nullptr)
            return {};
        const std::filesystem::path candidate =
            std::filesystem::path(base) / (std::string("game") + kModuleExtension);
        std::error_code ec;
        return std::filesystem::exists(candidate, ec) ? candidate.string() : std::string{};
    }

    std::string ResolveModulePath(const HostArguments& arguments)
    {
        if (arguments.GamePath.has_value() && !arguments.GamePath->empty())
            return *arguments.GamePath;
        if (const char* env = std::getenv("SENCHA_GAME_MODULE"); env != nullptr && env[0] != '\0')
            return env;
        return DefaultModuleBesideExe();
    }
}

int main(int argc, char** argv)
{
    const HostArguments arguments = ParseHostArguments(argc, argv);
    const bool headless = arguments.Headless;
    const std::vector<std::string>& contentRoots = arguments.ContentRoots;
    const std::string modulePath = ResolveModulePath(arguments);
    if (modulePath.empty())
    {
        std::fprintf(stderr,
            "Sencha: no game module.\n"
            "  Place game%s next to the executable, pass --game <path>, or set "
            "SENCHA_GAME_MODULE.\n",
            kModuleExtension);
        return 2;
    }

    GameModuleLoader loader;
    std::string error;
    LoadedModule module = loader.Load(modulePath, &error);
    if (!module.IsValid())
    {
        std::fprintf(stderr, "Sencha: failed to load game module '%s': %s\n",
                     modulePath.c_str(), error.c_str());
        return 1;
    }

    Application app(argc, argv);
    app.Configure([headless, &contentRoots, &arguments](EngineConfig& config) {
        // The fallback for a game that does not name itself in OnConfigure. A
        // game's name is also its settings namespace, so one that ships should.
        config.App.Name = "Sencha";
        // Where a player's settings persist, or nowhere: a desktop launch gets
        // the platform's configuration directory, a server nothing, and
        // --settings overrides either. The file inside is the game's to name.
        config.Console.SettingsRoot =
            ResolveSettingsRoot(arguments, (UserConfigDirectory() / "sencha").string());
        if (!contentRoots.empty())
            config.Runtime.ContentRoots = contentRoots;
        config.Window.Title = "Sencha";
        config.Window.Width = 1280;
        config.Window.Height = 720;
        ApplyEnvironmentOverrides(config);
        // A headless host builds no window and no graphics services. It still
        // runs the frame loop, so it ticks, streams, and drains async work like
        // any other host; it just has nothing to present.
        config.Window.GraphicsApi =
            headless ? WindowGraphicsApi::None : WindowGraphicsApi::Vulkan;
        if (headless)
        {
            // Nothing draws the overlay and nothing types into it. The console
            // itself still exists: the startup script and cvars are how a
            // headless host is driven.
            config.Console.UiEnabled = false;
            // And nobody sits in front of it: no pause shell, no options page,
            // no Back action. Separate from HasLocalPlayer below only in what
            // it governs -- presentation rather than possession.
            config.Runtime.ApplicationShell = false;

            // The two ways to reach a process with no window: a signal asking
            // it to stop, and a line typed at its terminal. Both are the host's
            // to own -- signals belong to the process, and naming the
            // descriptor rather than assuming one keeps the engine out of the
            // business of deciding whose standard input it is.
            InstallHostExitSignals();
            config.Runtime.HostExitFlag = &HostExitRequested;
            config.Console.CommandFd = HostCommandDescriptor();

            // Nobody is playing here. This is the dedicated-server posture,
            // decided once at the launch that asked for it: the authority
            // simulates every pawn and owns none of them. Separate from having
            // no graphics, which is the line above -- a scripted client is
            // headless with a player, and this switch is what tells them apart.
            config.Runtime.HasLocalPlayer = false;

            // A frame that presents nothing is still the frame that pumps the
            // network, and with no vsync to block on there is nothing else
            // pacing it. Two frames per fixed tick keeps a peer's command from
            // waiting more than half a tick to be read, and keeps a host off a
            // spinning core. Derived rather than written down twice: the tick
            // rate is the authority on simulation cadence, and `r.target_fps`
            // is the operator's override for both.
            config.Runtime.TargetFps = 2.0 * config.Runtime.FixedTickRate;
        }
    });

    const int result = app.Run(*module.Instance);

    // Teardown after the Game's OnShutdown ran inside Run, while still mapped.
    loader.Unload(module);
    return result;
}
