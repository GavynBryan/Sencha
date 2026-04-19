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
#ifdef SENCHA_ENABLE_DEBUG_UI
        config.Debug.DebugUi = true;
#endif
    });

    return app.Run<CubeDemoGame>();
}
