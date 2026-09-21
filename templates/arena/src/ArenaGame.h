#pragma once

#include <app/Game.h>
#include <core/console/ConsoleTypes.h>

#include "ArenaScore.h"
#include "ArenaSessionPolicy.h"

#include <app/BodySpawns.h>
#include <assets/data/DataAssetHandle.h>

#include <optional>

#include <string_view>

// The game module: what this game registers with the engine, what it puts on
// the console, and how it answers the two questions the participant lifecycle
// asks. Everything the run has loaded, and everything it took to load it,
// belongs to ArenaSessionPolicy.
class ArenaGame final : public Game
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
    void OnRegisterVocabulary(World& world) override;

private:

    // Constructed on the first OnStart, which is where the engine and its
    // logger exist; the game object itself is a module-static.
    std::optional<ArenaSessionPolicy> SessionState;
    [[nodiscard]] ArenaSessionPolicy& Session();
    void InstallScore(Engine& engine);

    // The engine's book on this game's prefab body requests. Closed in
    // OnShutdown, ahead of the session whose settings its callbacks read;
    // kept, closed, until the next OnStart replaces it.
    std::optional<BodySpawns> Bodies;

    // The game's one authored operation and the shell's bindings into it. The
    // token goes back in OnShutdown, while the dispatcher still exists.
    ArenaScoreOperation Score;
    VerbBindingToken ScoreBinding;
    DataAssetCacheHandle ShellBindingsAsset;
};
