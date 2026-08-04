#pragma once

#include <app/GameContexts.h>

#include <cassert>

class Engine;
class ComponentSerializerRegistry;
class DataAssetTypeRegistry;
class DataSchemaRegistry;
class WorldComponentSchema;

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

    // Structured data subtypes the module defines, plus their authoring
    // schemas. Registration only, like the serializer hooks: an editor calls
    // these standalone so it can author the module's data without starting
    // the game. The teardown must run while the module is still mapped —
    // the compiler is a std::function whose target lives in the module.
    virtual void OnRegisterDataAssetTypes(DataAssetTypeRegistry&, DataSchemaRegistry&) {}
    virtual void OnUnregisterDataAssetTypes(DataAssetTypeRegistry&, DataSchemaRegistry&) {}

    // Register the game's runtime ECS storage vocabulary. Engine::Run first adds
    // the engine-owned prefix, calls this hook, then seals the schema before
    // OnStart. Unlike serializer registration, this hook is runtime-only: the
    // editor does not need a live World merely to inspect scene fields.
    virtual void OnRegisterRuntimeComponents(WorldComponentSchema&) {}

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
