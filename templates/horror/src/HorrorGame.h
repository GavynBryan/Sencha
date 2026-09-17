#pragma once

#include <app/Game.h>
#include <core/console/ConsoleTypes.h>

#include "HorrorSessionPolicy.h"

#include <app/BodySpawns.h>

#include <optional>

#include <string_view>

// The game module: what this game registers with the engine, what it puts on
// the console, and how it answers the two questions the participant lifecycle
// asks. Everything the run has loaded, and everything it took to load it,
// belongs to HorrorSessionPolicy.
class HorrorGame final : public Game
{
public:
    void OnConfigure(GameConfigureContext& ctx) override;
    void OnRegisterComponents(ComponentRegistrar& registrar) override;
    void OnRegisterDataAssetTypes(DataAssetTypeRegistry& types,
                                  DataSchemaRegistry& schemas) override;
    void OnUnregisterDataAssetTypes(DataAssetTypeRegistry& types,
                                    DataSchemaRegistry& schemas) override;
    void OnStart(GameStartupContext& ctx) override;
    void OnRegisterSystems(SystemRegisterContext& ctx) override;
    void OnShutdown(GameShutdownContext& ctx) override;

private:

    // Constructed on the first OnStart, which is where the engine and its
    // logger exist; the game object itself is a module-static.
    std::optional<HorrorSessionPolicy> SessionState;
    [[nodiscard]] HorrorSessionPolicy& Session();

    // The engine's book on this game's prefab body requests. Closed in
    // OnShutdown, ahead of the session whose settings its callbacks read;
    // kept, closed, until the next OnStart replaces it.
    std::optional<BodySpawns> Bodies;
};
