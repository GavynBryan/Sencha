#include <app/GameModuleRun.h>

#include <app/Game.h>
#include <app/GameContexts.h>

#include <SDL3/SDL.h>

#include <utility>

GameModuleRun::GameModuleRun(const GameModuleRunSettings& settings)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    Module = Loader.Load(settings.Module, &Problem);
    if (!Module.IsValid())
        return;

    EngineConfig config;
    GameConfigureContext configure{ .Config = config };
    Module.Instance->OnConfigure(configure);
    if (settings.Headless)
        config.Window.GraphicsApi = WindowGraphicsApi::None;
    config.Runtime.ApplicationShell = false;
    config.Runtime.ContentRoots = { settings.ContentRoot };
    config.Runtime.TargetFps = settings.TargetFps;
    config.Console.UiEnabled = false;
    if (settings.Adjust)
        settings.Adjust(config);

    ConsoleStartupScript script;
    for (const ConsoleCommandLine& line : settings.Before)
        script.Add(line);
    script.Add(ConsoleCommandLine{ .Args = { "map", settings.Map } });
    script.Add(ConsoleCommandLine{ .Args = { "set", "app.exit_after_frames", std::to_string(settings.Frames) } });
    for (const ConsoleCommandLine& line : settings.After)
        script.Add(line);
    Runtime.emplace(std::move(config));
    Runtime->SetStartupScript(std::move(script));
}

GameModuleRun::~GameModuleRun()
{
    Shutdown();
}

int GameModuleRun::Run()
{
    return Runtime.has_value() ? Runtime->Run(*Module.Instance) : -1;
}

void GameModuleRun::Shutdown()
{
    if (Runtime.has_value())
        Runtime->Shutdown();
}
