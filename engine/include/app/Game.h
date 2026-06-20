#pragma once

#include <app/GameContexts.h>

#include <cassert>

class Engine;
class ComponentSerializerRegistry;

//=============================================================================
// Game
//
// Base interface for application-specific configuration and engine callbacks.
// Games override lifecycle hooks to register systems and react to platform
// events.
//
// The engine a game drives is bound once -- Engine::Run calls AttachEngine
// before the first lifecycle hook -- and reached through GetEngine(). That is
// why the contexts carry per-call data only, never an engine handle.
//=============================================================================
class Game
{
public:
    virtual ~Game() = default;

    virtual void OnConfigure(GameConfigureContext&) {}

    // Register the game's component serializers into the host registry. Called
    // standalone by the editor (so it can edit scenes containing game components
    // without ever starting the game) and by Engine::Run before any scene loads.
    // Registration only: no entities, no engine state. OnUnregisterComponents is
    // the symmetric teardown (editor module swap / host shutdown).
    virtual void OnRegisterComponents(ComponentSerializerRegistry&) {}
    virtual void OnUnregisterComponents(ComponentSerializerRegistry&) {}

    virtual void OnStart(GameStartupContext&) {}
    virtual void OnRegisterSystems(SystemRegisterContext&) {}
    virtual void OnPlatformEvent(PlatformEventContext&) {}
    virtual void OnShutdown(GameShutdownContext&) {}

    // Wiring, not behavior: Engine::Run binds itself once before any hook so a
    // game can reach the engine without threading a handle through contexts.
    void AttachEngine(Engine& engine) { BoundEngine = &engine; }

protected:
    [[nodiscard]] Engine& GetEngine() const
    {
        assert(BoundEngine && "GetEngine() before Engine::Run attached the engine");
        return *BoundEngine;
    }

private:
    Engine* BoundEngine = nullptr;
};
