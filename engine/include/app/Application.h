#pragma once

#include <app/Engine.h>
#include <app/Game.h>
#include <core/config/EngineConfig.h>

#include <memory>
#include <type_traits>
#include <utility>

//=============================================================================
// Application
//
// Owns process arguments and engine configuration before a game is launched.
// Provides the top-level entry point for configuring and running a Game.
//=============================================================================
class Application
{
public:
    Application(int argumentCount, char** argumentValues);

    [[nodiscard]] int Argc() const { return ArgumentCount; }
    [[nodiscard]] char** Argv() const { return ArgumentValues; }

    Application& WithEngineConfig(EngineConfig engineConfig);
    bool LoadEngineConfigFile(const char* path, EngineConfigError* error = nullptr);

    EngineConfig& Config() { return Configuration; }
    const EngineConfig& Config() const { return Configuration; }

    template<typename Fn>
    Application& Configure(Fn&& fn)
    {
        std::forward<Fn>(fn)(Configuration);
        return *this;
    }

    int Run(Game& game);

    template<typename TGame, typename... Args>
    int Run(Args&&... args)
    {
        static_assert(std::is_base_of_v<Game, TGame>, "TGame must derive from Game");
        auto game = std::make_unique<TGame>(std::forward<Args>(args)...);

        GameConfigureContext configure{
            .Config = Configuration,
        };
        game->OnConfigure(configure);

        Engine engine(Configuration);
        const int result = engine.Run(*game);
        game.reset();
        engine.Shutdown();
        return result;
    }

private:
    int ArgumentCount = 0;
    char** ArgumentValues = nullptr;
    EngineConfig Configuration;
};
