#pragma once

#include <app/GameContexts.h>

#include <cassert>

class Engine;
class ComponentRegistrar;
class DataAssetTypeRegistry;
class DataSchemaRegistry;
class World;

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

    // Name the components this game owns. One `registrar.Add<T>()` per
    // component, and what follows -- ECS storage, a scene serializer, a place
    // in the replicated table -- is read off each component's own TypeSchema.
    // A server-authoritative gameplay component is `Replicated = true` on its
    // schema and one line here; there is nothing else to keep in step and no
    // engine edit at all.
    //
    // Called standalone by the editor, which supplies a serializer registry and
    // nothing else so it can edit scenes containing game components without
    // starting the game, and by Engine::Run before any scene loads with all
    // three registries. Registration only: no entities, no engine state.
    //
    // Teardown is the host's: it retracts exactly what this added, while the
    // module is still mapped. Declaring a component and taking it back used to
    // be two lists that nothing forced to agree.
    virtual void OnRegisterComponents(ComponentRegistrar&) {}

    // Structured data subtypes the module defines, plus their authoring
    // schemas. Registration only, like the serializer hooks: an editor calls
    // these standalone so it can author the module's data without starting
    // the game. The teardown must run while the module is still mapped —
    // the compiler is a std::function whose target lives in the module.
    virtual void OnRegisterDataAssetTypes(DataAssetTypeRegistry&, DataSchemaRegistry&) {}
    virtual void OnUnregisterDataAssetTypes(DataAssetTypeRegistry&, DataSchemaRegistry&) {}

    virtual void OnStart(GameStartupContext&) {}
    virtual void OnRegisterSystems(SystemRegisterContext&) {}
    virtual void OnPlatformEvent(PlatformEventContext&) {}
    virtual void OnShutdown(GameShutdownContext&) {}

    // The gameplay vocabulary this game defines: its gameplay tags, attributes,
    // abilities, and locomotion modes, declared into one World's registries.
    //
    // These are registration-order runtime values, so they cannot travel in a
    // sealed component schema the way a component type does -- each World
    // installs them for itself. Content names them as strings and resolves them
    // at load, which is what makes this the editor's business too: an authoring
    // document that does not know the game's names cannot offer them in a
    // picker, and cannot tell an author that a tag it read back was one this
    // game never declared.
    //
    // Called by whoever owns the World -- the game's own OnStart for the runtime
    // world, once the engine vocabulary is installed, and the editor for each
    // authoring document it creates. Registration only: no entities, no
    // components, no engine state. Registries are idempotent, so declaring the
    // same name twice is the same as declaring it once.
    //
    // Lifetime: a registration can carry module code -- a locomotion mode's
    // enter and exit closures do -- and none of these registries can retract
    // one. So a World this was called on must be destroyed before the module is
    // unmapped, the way a module's data subtypes must be unregistered first.
    //
    // Last on purpose: a new hook takes a trailing vtable slot, so a module
    // built against an older header keeps every earlier slot at its old index.
    virtual void OnRegisterVocabulary(World&) {}

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
