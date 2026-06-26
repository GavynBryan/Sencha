#include <app/Application.h>

#include <optional>
#include <utility>

Application::Application(int argumentCount, char** argumentValues)
    : ArgumentCount(argumentCount)
    , ArgumentValues(argumentValues)
    , ArgvStartupScript(ConsoleStartupScript::FromArgv(argumentCount, argumentValues))
{
    RebuildStartupScriptFromConfig();
}

Application& Application::WithEngineConfig(EngineConfig engineConfig)
{
    Configuration = std::move(engineConfig);
    RebuildStartupScriptFromConfig();
    return *this;
}

bool Application::LoadEngineConfigFile(const char* path, EngineConfigError* error)
{
    std::optional<EngineConfig> loaded = LoadEngineConfig(path, error);
    if (!loaded)
        return false;

    Configuration = std::move(*loaded);
    RebuildStartupScriptFromConfig();
    return true;
}

int Application::Run(Game& game)
{
    GameConfigureContext configure{
        .Config = Configuration,
    };
    game.OnConfigure(configure);

    Engine engine(Configuration);
    engine.SetStartupScript(StartupScript);
    return engine.Run(game);
}

void Application::RebuildStartupScriptFromConfig()
{
    StartupScript.Clear();
    for (const std::string& path : Configuration.Console.ExecScripts)
    {
        StartupScript.Add(ConsoleCommandLine{
            .Args = { "exec", path },
            .Source = { .Description = "engine config" },
            .Text = "exec " + path,
        });
    }
    StartupScript.Append(ArgvStartupScript);
}
