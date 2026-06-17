#pragma once

#include <app/GameModuleAbi.h>
#include <app/ModuleExport.h>
#include <core/console/ConsoleRegistry.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <cstdint>
#include <string_view>

//=============================================================================
// IGameModule — the one stable contract a game module fulfils.
//
// A game module (game.so / game.dll) exposes exactly ONE C-linkage entry point,
// SenchaCreateGameModule(), returning a concrete IGameModule. Everything else
// crosses the boundary as C++ vtables created INSIDE the module (the pattern
// that already works for IComponentSerializer, 01-§1). No engine-global state
// lives in the module: it is handed the engine's single registries via the
// context and calls INTO them. See 02-...md §2.
//=============================================================================

// Read-only engine/host facts a module may inspect at registration time.
struct EngineHostInfo
{
    std::string_view EngineName  = "Sencha";
    std::uint32_t    AbiVersion  = SENCHA_GAME_ABI_VERSION;
};

// The inversion-of-control seam: references to the host's single registries.
// (Prefab and system/factory registries join this struct in S3/PIE, when their
// owning stages need them — added behind an ABI-version bump.)
struct GameModuleContext
{
    ComponentSerializerRegistry& Serializers;
    ConsoleRegistry&             Console;
    const EngineHostInfo&        Host;
};

struct IGameModule
{
    // Identity/versioning so the host can refuse an incompatible build.
    virtual std::string_view Name() const = 0;
    virtual std::uint32_t    AbiVersion() const = 0; // == SENCHA_GAME_ABI_VERSION at build

    // Called once at load: register components (serializers + storage), and later
    // prefabs/system factories, INTO the engine-owned registries in ctx.
    // MUST NOT create entities or touch a World's instance data — registration only.
    virtual void Register(GameModuleContext& ctx) = 0;

    // Called once at unload (editor module swap / host shutdown). Symmetric
    // teardown; frees anything the module handed the engine (module-owns rule).
    virtual void Unregister(GameModuleContext& ctx) = 0;

    virtual ~IGameModule() = default;
};

// The ONLY exported symbol of a game module. C linkage: resolved by a fixed name,
// no C++ mangling across the boundary.
extern "C" SENCHA_GAME_EXPORT IGameModule* SenchaCreateGameModule();
