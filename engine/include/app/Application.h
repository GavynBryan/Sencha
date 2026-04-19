#pragma once

#include <app/Engine.h>
#include <app/Game.h>
#include <core/config/EngineConfig.h>

#include <memory>
#include <type_traits>
#include <utility>

class Application
{
public:
    Application(int argc, char** argv);

    [[nodiscard]] int Argc() const { return Argc_; }
    [[nodiscard]] char** Argv() const { return Argv_; }

    Application& WithEngineConfig(EngineConfig config);
    bool LoadEngineConfigFile(const char* path, EngineConfigError* error = nullptr);

    EngineConfig& Config() { return Config_; }
    const EngineConfig& Config() const { return Config_; }

    template<typename Fn>
    Application& Configure(Fn&& fn)
    {
        std::forward<Fn>(fn)(Config_);
        return *this;
    }

    int Run(Game& game);

    template<typename TGame, typename... Args>
    int Run(Args&&... args)
    {
        static_assert(std::is_base_of_v<Game, TGame>, "TGame must derive from Game");
        auto game = std::make_unique<TGame>(std::forward<Args>(args)...);

        GameConfigureContext configure{
            .Config = Config_,
        };
        game->OnConfigure(configure);

        Engine engine(Config_);
        const int result = engine.Run(*game);
        game.reset();
        engine.Shutdown();
        return result;
    }

private:
    int Argc_ = 0;
    char** Argv_ = nullptr;
    EngineConfig Config_;
};
