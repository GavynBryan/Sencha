#pragma once

#include <assets/data/DataAssetHandle.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/console/ConsoleTypes.h>
#include <input/InputContextSet.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Engine;
class Logger;
struct CompiledPlatformerSettings;
struct SystemRegisterContext;

// The game's data vocabulary. One list, registered into whichever registries
// are asking: the session's own at startup, and the data editor's through the
// game module's OnRegisterDataAssetTypes hook.
void RegisterPlatformerDataTypes(DataAssetTypeRegistry& types,
                               DataSchemaRegistry& schemas);
void UnregisterPlatformerDataTypes(DataAssetTypeRegistry& types,
                                 DataSchemaRegistry& schemas);

//=============================================================================
// PlatformerSessionPolicy
//
// This game's own session state: which gameplay features its world carries,
// what it reads out of the engine's content, and the input context it plays in.
//
// The asset stack and the loaded level are both the engine's now, so what is
// left here is the half only this game can decide -- and its half of the
// lifetime rule the engine follows: every lease it takes is released in Close,
// while the caches that issued them still exist.
//
// The engine and the logger are named collaborators; nothing here reaches back
// into the game object that holds it.
//=============================================================================
class PlatformerSessionPolicy
{
public:
    PlatformerSessionPolicy(Engine& engine, Logger& log);
    ~PlatformerSessionPolicy();

    PlatformerSessionPolicy(const PlatformerSessionPolicy&) = delete;
    PlatformerSessionPolicy& operator=(const PlatformerSessionPolicy&) = delete;

    // Registers the gameplay features this game's world carries and binds the
    // controls it plays with.
    void Open();

    // Gives back everything this game holds into the engine's content stack.
    // Explicit rather than left to the destructor because the leases have to
    // drop while the caches that issued them still exist.
    void Close();

    // The shape cache the loaded level's collision loads into.
    void RegisterSystems(SystemRegisterContext& ctx);

    // The authored data this game reads, loaded on first ask and held for the
    // run. Read at every use, never cached by the caller: a hot reload swaps
    // the compiled value under the token and the next ask sees it.
    [[nodiscard]] const CompiledPlatformerSettings* GameSettings();

    // The engine's asset stack, for the startup wiring that reads this game's
    // authored data out of it.
    [[nodiscard]] RuntimeAssets& Assets();

private:
    [[nodiscard]] DataAssetCacheHandle AcquireDataAsset(std::string_view path);
    void SetupInputMapping();

    Engine& Host;
    Logger& Log;

    // Held for the run, released in Close before the caches are destroyed.
    DataAssetCacheHandle GameSettingsAsset;
    DataAssetCacheHandle InputActionSetAsset;
    DataAssetCacheHandle InputProfileAsset;
    // Held for the process: this game is always in its gameplay context. A
    // menu would take its own lease and drop this one.
    InputContextLease GameplayInput;
};
