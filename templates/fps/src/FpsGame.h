#pragma once

#include <app/Game.h>
#include <core/console/ConsoleTypes.h>

#include "FpsSessionPolicy.h"

#include <string_view>

// The game module: what this game registers with the engine, what it puts on
// the console, and how it answers the two questions the participant lifecycle
// asks. Everything the run has loaded, and everything it took to load it,
// belongs to FpsSessionPolicy.
class FpsGame final : public Game
{
public:
    void OnRegisterComponents(ComponentRegistrar& registrar) override;
    void OnRegisterDataAssetTypes(DataAssetTypeRegistry& types,
                                  DataSchemaRegistry& schemas) override;
    void OnUnregisterDataAssetTypes(DataAssetTypeRegistry& types,
                                    DataSchemaRegistry& schemas) override;
    void OnStart(GameStartupContext& ctx) override;
    void OnRegisterSystems(SystemRegisterContext& ctx) override;
    void OnPlatformEvent(PlatformEventContext& ctx) override;
    void OnShutdown(GameShutdownContext& ctx) override;

private:

    // Constructed on the first OnStart, which is where the engine and its
    // logger exist; the game object itself is a module-static.
    std::optional<FpsSessionPolicy> SessionState;
    [[nodiscard]] FpsSessionPolicy& Session();
};
