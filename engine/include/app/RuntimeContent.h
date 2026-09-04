#pragma once

#include <assets/runtime/ContentMount.h>
#include <assets/runtime/RuntimeAssets.h>
#include <world/serialization/SceneSerializationContext.h>

#ifdef SENCHA_ENABLE_COOK
#include <assets/cook/AssetImporter.h>
#include <assets/hotreload/AssetHotReloader.h>
#include <assets/hotreload/AssetSourceWatcher.h>
#endif

#include <memory>
#include <optional>
#include <span>
#include <vector>

class Engine;
class EngineSchedule;
class Logger;
class World;

//=============================================================================
// RuntimeContent
//
// The content a running process holds: the asset stack composed for what this
// process can carry, the roots it mounted into that stack, and the engine
// services pointed at it.
//
// It exists because those are one lifetime rather than several. A reference
// into the asset stack that outlives the stack calls through a destroyed vtable
// at shutdown -- a crash on the way out rather than at the mistake -- so the
// order in which those references are given back is a property worth having one
// owner for. Mount and Publish compose, Disconnect gives back, and the members
// are declared so that destruction alone would do the same thing.
//
// A level is not content: what is loaded right now has its own lifetime, is
// unloaded and reloaded while this stays up, and is owned by LoadedLevel. Hot
// reload, by the same rule, belongs here -- it watches the roots this mounted
// and reloads into the stack this owns, whatever level happens to be resident.
//
// The engine and the logger are named collaborators; nothing here reaches into
// the game.
//=============================================================================
class RuntimeContent
{
public:
    RuntimeContent(Engine& engine, Logger& log);
    ~RuntimeContent();

    RuntimeContent(const RuntimeContent&) = delete;
    RuntimeContent& operator=(const RuntimeContent&) = delete;

    // Mounts every root in RuntimeConfig::ContentRoots, in order. Under a
    // cook-enabled build it also starts watching those same roots so authored
    // data reloads in place while the process runs.
    void Mount();

    // Publishes the stack: the world resources that read through it, the spawn
    // services that resolve scenes through it, and the render pipeline's stores.
    void Publish(World& world);

    // The systems that keep mounted content current. Cook-gated: a shipping
    // build watches nothing and registers nothing.
    void RegisterSystems(EngineSchedule& schedule);

    // The exact reverse of Publish, plus the world-resource caches that hold
    // leases into this stack. Idempotent, and required before destruction:
    // nothing may hold a lease into Assets() after it returns.
    void Disconnect(World& world);

    [[nodiscard]] RuntimeAssets& Assets();
    [[nodiscard]] const RuntimeAssets& Assets() const;

    // The one context zone loads and scene spawns resolve handles through.
    [[nodiscard]] SceneSerializationContext& SceneContext();

    // The roots Mount resolved, in configuration order.
    [[nodiscard]] std::span<const ContentRootPaths> Roots() const;

#ifdef SENCHA_ENABLE_COOK
    // One watcher and reloader per mounted root, because both sides are built
    // around a single root: the watcher walks it and reports paths relative to
    // it, and the reloader re-cooks against it. Public because the poll system
    // that drives them is a free system, not a member.
    struct WatchedRoot
    {
        AssetHotReloader Reloader;
        AssetSourceWatcher Watcher;
    };
#endif

private:
    Engine& Host;
    Logger& Log;

    // Declaration order is the destruction contract, and Disconnect mirrors it:
    // whatever holds a reference into the asset stack goes before the stack.
    std::optional<RuntimeAssets> Assets_;
    std::unique_ptr<SceneSerializationContext> SceneContextState;

#ifdef SENCHA_ENABLE_COOK
    // Dev-only source watching so authored data reloads in place while the
    // process runs. No importers: the watched formats are runtime formats.
    AssetImporterRegistry HotReloadImporters;
    std::vector<std::unique_ptr<WatchedRoot>> HotReloadRoots;
#endif

    std::vector<ContentRootPaths> MountedRoots;
    bool Published = false;
};
