#include "CubeDemoGame.h"

#include <app/Application.h>
#include <core/config/EngineConfig.h>

int main(int argc, char** argv)
{
    Application app(argc, argv);
    app.Configure([](EngineConfig& config) {
        config.App.Name = "Sencha Cube Demo";
        config.Window.Title = "Sencha Cube Demo";
        config.Window.Width = 1280;
        config.Window.Height = 720;
        config.Window.GraphicsApi = WindowGraphicsApi::Vulkan;
        config.Runtime.ExitOnEscape = true;
        config.Runtime.TogglePauseOnF1 = true;

        // Sfx bus for scene AudioSourceComponents (docs/audio/runtime.md):
        // looping ambients belong on a Reject bus with voice headroom so a
        // steal never silences them (Decision E).
        EngineAudioBusConfig sfx;
        sfx.Name = "Sfx";
        sfx.MaxVoices = 8;
        sfx.StealPolicy = VoiceStealPolicy::Reject;
        config.Audio.Buses.push_back(std::move(sfx));
#ifdef SENCHA_ENABLE_DEBUG_UI
        config.Debug.DebugUi = true;
#endif
    });

    return app.Run<CubeDemoGame>();
}
