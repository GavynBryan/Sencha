#pragma once

#include <app/Engine.h>
#include <app/EngineSchedule.h>
#include <app/Game.h>
#include <app/GameContexts.h>
#include <app/GameModuleLoader.h>
#include <core/config/EngineConfig.h>
#include <core/console/ConsoleStartupScript.h>

#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include <string>
#include <string_view>

//=============================================================================
// Runs a built template module the way the host does: loaded through the real
// module path, configured by its own OnConfigure, given a content root and a
// map, run headless for a fixed number of frames. The engine outlives the run
// so a case can read the world it left behind; the module outlives the engine
// because the engine still holds the module's registrations until Shutdown.
//
// `Probe` is a system the test registers on the engine before the run. It sees
// the world every frame, which is the only honest place to observe a template's
// composition: the game's own teardown runs before Run returns.
//=============================================================================
template <typename Probe>
class TemplateModuleRun
{
public:
    TemplateModuleRun(std::string_view modulePath, std::string_view contentRoot,
                      std::string_view map, int frames)
    {
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

        std::string error;
        Module = Loader.Load(std::string(modulePath), &error);
        if (!Module.IsValid())
        {
            ADD_FAILURE() << "module did not load: " << error;
            return;
        }

        EngineConfig config;
        GameConfigureContext configure{ .Config = config };
        Module.Instance->OnConfigure(configure);
        config.Window.GraphicsApi = WindowGraphicsApi::None;
        config.Debug.ConsoleLogging = false;
        config.Runtime.TargetFps = 1000.0;
        config.Runtime.ContentRoots = { std::string(contentRoot) };

        ConsoleStartupScript script;
        script.Add(ConsoleCommandLine{
            .Args = { "map", std::string(map) },
            .Source = { .Description = "template test" },
            .Text = "map " + std::string(map) });
        script.Add(ConsoleCommandLine{
            .Args = { "set", "app.exit_after_frames", std::to_string(frames) },
            .Source = { .Description = "template test" },
            .Text = "set app.exit_after_frames " + std::to_string(frames) });

        Runtime.emplace(config);
        Runtime->SetStartupScript(script);
        Observed = &Runtime->Schedule().Register<Probe>();
        if constexpr (requires { Observed->Host = &*Runtime; })
            Observed->Host = &*Runtime;
        ExitCode = Runtime->Run(*Module.Instance);
    }

    ~TemplateModuleRun()
    {
        if (Runtime.has_value())
            Runtime->Shutdown();
    }

    [[nodiscard]] bool Loaded() const { return Module.IsValid(); }
    [[nodiscard]] int Exit() const { return ExitCode; }
    [[nodiscard]] const Probe& Seen() const { return *Observed; }

private:
    GameModuleLoader Loader;
    LoadedModule Module;
    std::optional<Engine> Runtime;
    Probe* Observed = nullptr;
    int ExitCode = -1;
};
