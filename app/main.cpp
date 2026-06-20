#include <app/Application.h>
#include <app/Game.h>
#include <app/GameModuleLoader.h>
#include <core/config/EngineConfig.h>

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

//=============================================================================
// app: the Sencha runtime host.
//
// Pure glue: it loads a game module (which provides a Game), runs it, and
// unloads. It carries no per-project code; a game is a module + content, and
// the same binary is what the editor spawns for Play-In-Editor. The module is
// resolved as --game <path>, then $SENCHA_GAME_MODULE, then the default game
// module beside the executable (game.so / .dll / .dylib) -- so a bundle of
// app + game module runs with a bare `app +map levels/foo`. Everything else
// (the +commands) flows through the engine's startup script.
//=============================================================================

namespace
{
#if defined(_WIN32)
    constexpr const char* kModuleExtension = ".dll";
#elif defined(__APPLE__)
    constexpr const char* kModuleExtension = ".dylib";
#else
    constexpr const char* kModuleExtension = ".so";
#endif

    // Strips --game <path> from argv (so the remaining +commands reach the engine
    // startup script regardless of order) and returns the path, or empty.
    std::string ExtractGameArg(int& argc, char** argv)
    {
        std::string path;
        int write = 1;
        for (int read = 1; read < argc; ++read)
        {
            if (std::strcmp(argv[read], "--game") == 0 && read + 1 < argc)
            {
                path = argv[read + 1];
                ++read; // skip the value too
                continue;
            }
            argv[write++] = argv[read];
        }
        argc = write;
        return path;
    }

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

    std::string ResolveModulePath(int& argc, char** argv)
    {
        if (std::string arg = ExtractGameArg(argc, argv); !arg.empty())
            return arg;
        if (const char* env = std::getenv("SENCHA_GAME_MODULE"); env != nullptr && env[0] != '\0')
            return env;
        return DefaultModuleBesideExe();
    }
}

int main(int argc, char** argv)
{
    const std::string modulePath = ResolveModulePath(argc, argv);
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
    app.Configure([](EngineConfig& config) {
        config.App.Name = "Sencha";
        config.Window.Title = "Sencha";
        config.Window.Width = 1280;
        config.Window.Height = 720;
        config.Window.GraphicsApi = WindowGraphicsApi::Vulkan;
        config.Runtime.ExitOnEscape = true;
        config.Runtime.TogglePauseOnF1 = true;
        config.Debug.DebugUi = true;
    });

    const int result = app.Run(*module.Instance);

    // Teardown after the Game's OnShutdown ran inside Run, while still mapped.
    loader.Unload(module);
    return result;
}
