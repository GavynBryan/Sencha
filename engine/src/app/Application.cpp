#include <app/Application.h>

#include <optional>
#include <utility>

Application::Application(int argumentCount, char** argumentValues)
    : ArgumentCount(argumentCount)
    , ArgumentValues(argumentValues)
{
}

Application& Application::WithEngineConfig(EngineConfig engineConfig)
{
    Configuration = std::move(engineConfig);
    return *this;
}

bool Application::LoadEngineConfigFile(const char* path, EngineConfigError* error)
{
    std::optional<EngineConfig> loaded = LoadEngineConfig(path, error);
    if (!loaded)
        return false;

    Configuration = std::move(*loaded);
    return true;
}

int Application::Run(Game& game)
{
    GameConfigureContext configure{
        .Config = Configuration,
    };
    game.OnConfigure(configure);

    Engine engine(Configuration);
    return engine.Run(game);
}
