#include <app/Application.h>

#include <optional>
#include <utility>

Application::Application(int argc, char** argv)
    : Argc_(argc)
    , Argv_(argv)
{
}

Application& Application::WithEngineConfig(EngineConfig config)
{
    Config_ = std::move(config);
    return *this;
}

bool Application::LoadEngineConfigFile(const char* path, EngineConfigError* error)
{
    std::optional<EngineConfig> loaded = LoadEngineConfig(path, error);
    if (!loaded)
        return false;

    Config_ = std::move(*loaded);
    return true;
}

int Application::Run(Game& game)
{
    GameConfigureContext configure{
        .Config = Config_,
    };
    game.OnConfigure(configure);

    Engine engine(Config_);
    return engine.Run(game);
}
